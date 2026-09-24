// Child processes on POSIX: posix_spawn with its own pipes and process group.

#include "platform/rv_editor_process.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace rv_editor
{

namespace
{

// A write to a pipe whose reader has died must fail with EPIPE, not kill the
// editor with SIGPIPE.
void rv_editor_ignore_sigpipe()
{
    static std::once_flag once;
    std::call_once(once, [] { std::signal(SIGPIPE, SIG_IGN); });
}

void rv_editor_close(int &fd)
{
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

bool rv_editor_pipe(int fds[2])
{
    return ::pipe2(fds, O_CLOEXEC) == 0;
}

// Drains one non-blocking pipe. Closes it at end of file.
void rv_editor_drain(int &fd, std::string &into, size_t limit)
{
    char buf[16384];
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
    }
    close_fds();
}

void rv_editor_process::close_fds()
{
    rv_editor_close(in_);
    rv_editor_close(out_);
    rv_editor_close(err_);
}

bool rv_editor_process::start(const std::vector<std::string> &argv, const std::filesystem::path &cwd,
    std::string &error)
{
    rv_editor_ignore_sigpipe();
    if (argv.empty() || running()) {
        error = argv.empty() ? "nothing to run" : "already running";
        return false;
    }
    close_fds();
    pending_.clear();
    exit_ = {};
    pid_ = -1;

    int in[2] = { -1, -1 };
    int out[2] = { -1, -1 };
    int err[2] = { -1, -1 };
    if (!rv_editor_pipe(in) || !rv_editor_pipe(out) || !rv_editor_pipe(err)) {
        error = std::string("pipe: ") + std::strerror(errno);
        for (int fd : { in[0], in[1], out[0], out[1], err[0], err[1] }) {
            if (fd >= 0) {
                ::close(fd);
            }
        }
        return false;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawn_file_actions_adddup2(&actions, err[1], 2);
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
    const int rc = posix_spawn(&pid, argv[0].c_str(), &actions, &attr, args.data(), environ);
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
        return false;
    }

    pid_ = pid;
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
            pid_ = -1;
            close_fds();
            return false;
        }
    }
    return true;
}

bool rv_editor_process::read(std::string &out, std::string &err, size_t limit)
{
    rv_editor_drain(out_, out, limit);
    rv_editor_drain(err_, err, limit);
    return out_ >= 0 || err_ >= 0;
}

bool rv_editor_process::write(std::string_view bytes)
{
    if (in_ < 0) {
        return false;
    }
    pending_.append(bytes);
    flush();
    return in_ >= 0;
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
    rv_editor_close(in_);
    return true;
}

void rv_editor_process::stop(bool force)
{
    if (!running()) {
        return;
    }
    const int sig = force ? SIGKILL : SIGTERM;
    if (::kill(-pid_, sig) != 0) {
        ::kill(pid_, sig);
    }
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

} // namespace rv_editor
