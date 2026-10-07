// Child processes on POSIX: posix_spawn with its own pipes and process group.

#include "platform/rv_editor_process.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pdk/rv_err.h"

extern char **environ;

namespace rv_editor
{

namespace
{

// Current directory marker used when PATH component is empty
constexpr const char *current_dir_marker = ".";

// How long a reaped child's output may keep arriving from a grandchild that
// still holds a pipe open, before the process gives up and calls it cut.
constexpr auto rv_editor_output_grace = std::chrono::seconds(2);

// Read buffer size for draining subprocess output
constexpr size_t read_buffer_size = 16384;

// Frame descriptor number passed to console: must match "--frame-fd" in rv_editor_session.cpp
constexpr int inherit_fd_number = 3;

// PATH environment variable component separator
constexpr char path_separator = ':';

// PATH environment variable name for executable search
constexpr const char *env_var_path = "PATH";

// A write to a pipe whose reader has died must fail with EPIPE, not kill the
// editor with SIGPIPE.
void rv_editor_ignore_sigpipe()
{
    static std::once_flag once;
    std::call_once(once, [] {
        std::signal(SIGPIPE, SIG_IGN);
    });
}

void rv_editor_close(int &fd)
{
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

int rv_editor_pipe(int fds[2])
{
    return ::pipe2(fds, O_CLOEXEC) == 0 ? RV_OK : RV_ERR_IO;
}

// Drains one non-blocking pipe. Closes it at end of file.
void rv_editor_drain(int &fd, std::string &into, size_t limit)
{
    char buf[read_buffer_size];
    size_t taken = 0;
    while (fd >= 0 && taken < limit) {
        const ssize_t n = ::read(fd, buf, std::min(sizeof(buf), limit - taken));
        if (n > 0) {
            into.append(buf, static_cast<size_t>(n));
            taken += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n == 0) {
            rv_editor_close(fd);
        }
        return;
    }
}

} // namespace

rv_editor_process::~rv_editor_process()
{
    if (running()) {
        stop(true);
        int status = 0;
        while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
        }
        rv_editor_guard_remove(pid_);
    }
    close_fds();
}

void rv_editor_process::close_fds()
{
    rv_editor_close(in_);
    rv_editor_close(out_);
    rv_editor_close(err_);
}

int rv_editor_process::start(const std::vector<std::string> &argv,
    const std::filesystem::path &cwd,
    std::string &error,
    int inherit_fd,
    const std::vector<std::string> &env)
{
    rv_editor_ignore_sigpipe();
    if (argv.empty() || running()) {
        error = argv.empty() ? "nothing to run" : "already running";
        return RV_ERR_INVAL;
    }
    close_fds();
    pending_.clear();
    exit_ = {};
    pid_ = -1;
    cut_ = false;

    int in[2] = { -1, -1 };
    int out[2] = { -1, -1 };
    int err[2] = { -1, -1 };
    if (rv_editor_pipe(in) != RV_OK || rv_editor_pipe(out) != RV_OK || rv_editor_pipe(err) != RV_OK) {
        error = std::string("pipe: ") + std::strerror(errno);
        for (int fd : { in[0], in[1], out[0], out[1], err[0], err[1] }) {
            if (fd >= 0) {
                ::close(fd);
            }
        }
        return RV_ERR_IO;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err[1], STDERR_FILENO);
    // dup2 leaves the copy without close-on-exec, so this one descriptor crosses.
    if (inherit_fd >= 0) {
        posix_spawn_file_actions_adddup2(&actions, inherit_fd, inherit_fd_number);
    }
    if (!cwd.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    }

    // Its own process group, and SIGPIPE back to the default the editor turned off.
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF);
    posix_spawnattr_setpgroup(&attr, 0);
    sigset_t defaults;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigdefault(&attr, &defaults);

    std::vector<char *> args;
    for (const std::string &a : argv) {
        args.push_back(const_cast<char *>(a.c_str()));
    }
    args.push_back(nullptr);

    pid_t pid = -1;
    // The inherited environment, less each key `env` sets again, then `env`.
    std::vector<std::string> merged;
    for (char **e = environ; *e != nullptr; ++e) {
        const std::string_view entry(*e);
        const std::string_view key = entry.substr(0, entry.find('='));
        const bool replaced = std::any_of(env.begin(), env.end(), [key](const std::string &o) {
            return o.size() > key.size() && o.compare(0, key.size(), key) == 0 && o[key.size()] == '=';
        });
        if (!replaced) {
            merged.emplace_back(entry);
        }
    }
    merged.insert(merged.end(), env.begin(), env.end());
    std::vector<char *> envp;
    for (std::string &e : merged) {
        envp.push_back(e.data());
    }
    envp.push_back(nullptr);

    const int rc = posix_spawn(&pid, argv[0].c_str(), &actions, &attr, args.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    ::close(in[0]);
    ::close(out[1]);
    ::close(err[1]);
    if (rc != 0) {
        ::close(in[1]);
        ::close(out[0]);
        ::close(err[0]);
        error = argv[0] + ": " + std::strerror(rc);
        if (rc == ENOENT) {
            return RV_ERR_NOENT;
        }
        if (rc == EINVAL) {
            return RV_ERR_INVAL;
        }
        return RV_ERR_IO;
    }

    pid_ = pid;
    rv_editor_guard_add(pid);
    in_ = in[1];
    out_ = out[0];
    err_ = err[0];
    for (int fd : { in_, out_, err_ }) {
        const int flags = ::fcntl(fd, F_GETFL);
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            // A blocking pipe would stall the UI thread: this child is not kept.
            error = std::string("fcntl: ") + std::strerror(errno);
            stop(true);
            while (::waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
            }
            rv_editor_guard_remove(pid_);
            pid_ = -1;
            close_fds();
            return RV_ERR_IO;
        }
    }
    return RV_OK;
}

bool rv_editor_process::read(std::string &out, std::string &err, size_t limit)
{
    rv_editor_drain(out_, out, limit);
    rv_editor_drain(err_, err, limit);
    return out_ >= 0 || err_ >= 0;
}

int rv_editor_process::write(std::string_view bytes)
{
    if (in_ < 0) {
        return RV_ERR_IO;
    }
    if (pending_.size() + bytes.size() > input_max) {
        flush();
        if (pending_.size() + bytes.size() > input_max) {
            return RV_ERR_BUSY;
        }
    }
    pending_.append(bytes);
    flush();
    return in_ >= 0 ? RV_OK : RV_ERR_IO;
}

void rv_editor_process::flush()
{
    while (in_ >= 0 && !pending_.empty()) {
        const ssize_t n = ::write(in_, pending_.data(), pending_.size());
        if (n > 0) {
            pending_.erase(0, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && errno == EAGAIN) {
            return;
        }
        // EPIPE: the reader is gone; nothing queued can ever arrive.
        pending_.clear();
        rv_editor_close(in_);
    }
}

void rv_editor_process::close_stdin()
{
    flush();
    rv_editor_close(in_);
}

bool rv_editor_process::poll()
{
    if (pid_ <= 0 || exit_.exited) {
        return exit_.exited;
    }
    int status = 0;
    const pid_t r = ::waitpid(pid_, &status, WNOHANG);
    if (r != pid_) {
        return false;
    }
    exit_.exited = true;
    if (WIFEXITED(status)) {
        exit_.code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_.signal = WTERMSIG(status);
    }
    exited_at_ = std::chrono::steady_clock::now();
    rv_editor_close(in_);
    rv_editor_guard_remove(pid_);
    return true;
}

bool rv_editor_process::output_done()
{
    if (out_ < 0 && err_ < 0) {
        return true;
    }
    if (!exit_.exited || std::chrono::steady_clock::now() - exited_at_ < rv_editor_output_grace) {
        return false;
    }
    cut_ = true;
    close_fds();
    return true;
}

int rv_editor_process::stop(bool force)
{
    if (!running()) {
        return RV_OK;
    }
    const int sig = force ? SIGKILL : SIGTERM;
    const bool group_ok = ::kill(-pid_, sig) == 0;
    if (!group_ok && ::kill(pid_, sig) != 0) {
        return RV_ERR_IO;
    }
    return RV_OK;
}

std::string rv_editor_exit_text(const rv_editor_process::rv_editor_exit &exit)
{
    if (!exit.exited) {
        return "running";
    }
    if (exit.signal != 0) {
        const char *name = sigabbrev_np(exit.signal);
        return std::string("killed by SIG") + (name != nullptr ? name : std::to_string(exit.signal));
    }
    return "exit code " + std::to_string(exit.code);
}

std::filesystem::path rv_editor_process_find(const char *name)
{
    const char *path = std::getenv(env_var_path);
    if (path == nullptr) {
        return {};
    }
    std::stringstream dirs(path);
    std::string dir;
    while (std::getline(dirs, dir, path_separator)) {
        std::error_code ec;
        const std::filesystem::path p = std::filesystem::path(dir.empty() ? current_dir_marker : dir) / name;
        const auto st = std::filesystem::status(p, ec);
        if (!ec && std::filesystem::is_regular_file(st) &&
            (st.permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
            return p;
        }
    }
    return {};
}

} // namespace rv_editor
