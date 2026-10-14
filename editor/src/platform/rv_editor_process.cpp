// Child processes on POSIX: fork/exec through rv_editor_spawn with its own pipes and process group.

#include "platform/rv_editor_process.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pdk/rv_err.h"
#include "platform/rv_editor_spawn.hpp"

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

// Drains one non-blocking pipe. Closes it at end of file or on a read error.
// EAGAIN is not an error: the child has not written yet.
int rv_editor_drain(int &fd, std::string &into, size_t limit, std::string &error)
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
        if (n < 0 && errno != EAGAIN) {
            error = std::string("read: ") + std::strerror(errno);
            rv_editor_close(fd);
            return RV_ERR_IO;
        }
        if (n == 0) {
            rv_editor_close(fd);
        }
        return RV_OK;
    }
    return RV_OK;
}

// Kills a child's group and reaps the child: start() keeps no child it cannot guard.
void rv_editor_kill_reap(pid_t pid)
{
    ::kill(-pid, SIGKILL);
    while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
    }
}

// Exit status of a detached child that failed: its errno is in the report pipe.
constexpr int detached_failed_exit_code = 127;

// Child side, after fork: errno to the report pipe, then exit. Only async-signal-safe calls.
[[noreturn]] void rv_editor_detach_fail(int report_fd, int err)
{
    (void)!::write(report_fd, &err, sizeof(err));
    ::_exit(detached_failed_exit_code);
}

// Grandchild, after fork: stdio to /dev/null, then exec. argv is built before fork.
[[noreturn]] void rv_editor_detach_exec(char *const argv[], int report_fd)
{
    const int null_fd = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    if (null_fd < 0) {
        rv_editor_detach_fail(report_fd, errno);
    }
    for (int fd : { STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO }) {
        // With fd closed, null_fd is fd: dup2 would be a no-op that keeps close-on-exec.
        const int rc = null_fd == fd ? ::fcntl(fd, F_SETFD, 0) : ::dup2(null_fd, fd);
        if (rc < 0) {
            rv_editor_detach_fail(report_fd, errno);
        }
    }
    sigset_t none;
    ::sigemptyset(&none);
    if (::sigprocmask(SIG_SETMASK, &none, nullptr) != 0) {
        rv_editor_detach_fail(report_fd, errno);
    }
    // The editor ignores SIGPIPE; the launched program gets the default back.
    ::signal(SIGPIPE, SIG_DFL);
    ::execv(argv[0], argv);
    rv_editor_detach_fail(report_fd, errno);
}

// Middle child, after fork: a session of its own, then the grandchild. Exits 0 at once,
// so the parent reaps it without waiting for the launched program.
[[noreturn]] void rv_editor_detach_middle(char *const argv[], int report_fd)
{
    // Above fd 2: the grandchild's dup2 onto 0-2 would overwrite a lower report end.
    const int lifted = ::fcntl(report_fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    if (lifted < 0) {
        rv_editor_detach_fail(report_fd, errno);
    }
    report_fd = lifted;
    if (::setsid() < 0) {
        rv_editor_detach_fail(report_fd, errno);
    }
    const pid_t grandchild = ::fork();
    if (grandchild < 0) {
        rv_editor_detach_fail(report_fd, errno);
    }
    if (grandchild == 0) {
        rv_editor_detach_exec(argv, report_fd);
    }
    ::_exit(0);
}

// Reads the report after the middle is reaped. EOF: no errno was written: the grandchild exec'd
// (or died before writing). An errno: it failed before exec.
int rv_editor_detach_report(int fd, const std::string &program, std::string &error)
{
    int child_errno = 0;
    ssize_t n = 0;
    do {
        n = ::read(fd, &child_errno, sizeof(child_errno));
    } while (n < 0 && errno == EINTR);
    if (n == 0) {
        return RV_OK;
    }
    if (n < 0) {
        error = std::string("exec report: ") + std::strerror(errno);
        return RV_ERR_IO;
    }
    if (n != static_cast<ssize_t>(sizeof(child_errno))) {
        error = program + ": exec report incomplete";
        return RV_ERR_IO;
    }
    error = program + ": " + std::strerror(child_errno);
    return RV_ERR_IO;
}

} // namespace

rv_editor_process::~rv_editor_process()
{
    if (running()) {
        stop(true);
        int status = 0;
        while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
        }
        // A failed remove has nowhere to go here: a dead guard refuses the next start().
        std::string guard_error;
        rv_editor_guard_remove(pid_, guard_error);
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

    rv_editor_spawn_setup setup;
    setup.group = rv_editor_spawn_group::own;
    setup.dup_pairs = { { in[0], STDIN_FILENO }, { out[1], STDOUT_FILENO }, { err[1], STDERR_FILENO } };
    // dup2 leaves the copy without close-on-exec, so this one descriptor crosses.
    if (inherit_fd >= 0) {
        setup.dup_pairs.emplace_back(inherit_fd, inherit_fd_number);
    }
    setup.cwd = cwd;
    // SIGPIPE back to the default the editor turned off.
    sigemptyset(&setup.signals_default);
    sigaddset(&setup.signals_default, SIGPIPE);

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

    const int rc = rv_editor_spawn(argv[0].c_str(), args.data(), envp.data(), setup, pid, error);
    ::close(in[0]);
    ::close(out[1]);
    ::close(err[1]);
    if (rc != RV_OK) {
        ::close(in[1]);
        ::close(out[0]);
        ::close(err[0]);
        return rc;
    }

    pid_ = pid;
    in_ = in[1];
    out_ = out[0];
    err_ = err[0];
    std::string guard_error;
    if (rv_editor_guard_add(pid, guard_error) != RV_OK) {
        error = "process guard: " + guard_error;
        rv_editor_kill_reap(pid);
        pid_ = -1;
        close_fds();
        return RV_ERR_IO;
    }
    for (int fd : { in_, out_, err_ }) {
        const int flags = ::fcntl(fd, F_GETFL);
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            // A blocking pipe would stall the UI thread: this child is not kept.
            error = std::string("fcntl: ") + std::strerror(errno);
            rv_editor_kill_reap(pid);
            std::string remove_error;
            if (rv_editor_guard_remove(pid, remove_error) != RV_OK) {
                error += "; process guard: " + remove_error;
            }
            pid_ = -1;
            close_fds();
            return RV_ERR_IO;
        }
    }
    return RV_OK;
}

int rv_editor_process::read(std::string &out, std::string &err, size_t limit, std::string &error)
{
    std::string err_error;
    const int out_rc = rv_editor_drain(out_, out, limit, error);
    const int err_rc = rv_editor_drain(err_, err, limit, err_error);
    if (out_rc != RV_OK) {
        return out_rc;
    }
    if (err_rc != RV_OK) {
        error = err_error;
        return err_rc;
    }
    return RV_OK;
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
    exit_.guard_result = rv_editor_guard_remove(pid_, exit_.guard_error);
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
        return RV_ERR_ALREADY;
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
    std::string text = "exit code " + std::to_string(exit.code);
    if (exit.signal != 0) {
        const char *name = sigabbrev_np(exit.signal);
        text = std::string("killed by SIG") + (name != nullptr ? name : std::to_string(exit.signal));
    }
    if (exit.guard_result != RV_OK) {
        text += "; process guard: " + exit.guard_error;
    }
    return text;
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

int rv_editor_launch_detached(const std::vector<std::string> &argv, std::string &error)
{
    if (argv.empty()) {
        error = "nothing to run";
        return RV_ERR_INVAL;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(argv[0], ec) || ::access(argv[0].c_str(), X_OK) != 0) {
        error = argv[0] + ": not an executable file";
        return RV_ERR_NOENT;
    }
    std::vector<char *> args;
    for (const std::string &a : argv) {
        args.push_back(const_cast<char *>(a.c_str()));
    }
    args.push_back(nullptr);

    int fds[2] = { -1, -1 };
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        error = std::string("pipe: ") + std::strerror(errno);
        return RV_ERR_IO;
    }
    const pid_t middle = ::fork();
    if (middle < 0) {
        const int fork_errno = errno;
        ::close(fds[0]);
        ::close(fds[1]);
        error = std::string("fork: ") + std::strerror(fork_errno);
        return RV_ERR_IO;
    }
    if (middle == 0) {
        ::close(fds[0]);
        rv_editor_detach_middle(args.data(), fds[1]);
    }
    ::close(fds[1]);
    int status = 0;
    while (::waitpid(middle, &status, 0) < 0) {
        if (errno != EINTR) {
            ::close(fds[0]);
            error = std::string("waitpid: ") + std::strerror(errno);
            return RV_ERR_IO;
        }
    }
    const int rc = rv_editor_detach_report(fds[0], argv[0], error);
    ::close(fds[0]);
    if (rc != RV_OK) {
        return rc;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = argv[0] + ": the launcher child ended abnormally";
        return RV_ERR_IO;
    }
    return RV_OK;
}

} // namespace rv_editor
