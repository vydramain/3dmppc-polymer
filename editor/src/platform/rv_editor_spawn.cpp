// Child start on POSIX: fork and execve, so the child can ask for SIGKILL when the editor dies.
// posix_spawn cannot set PR_SET_PDEATHSIG. fork copies the editor's page tables, unlike posix_spawn;
// that cost is accepted for PDEATHSIG.

#include "platform/rv_editor_spawn.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Exit status of a child that failed before exec. Its errno is in the report pipe; the getppid
// check exits with it too, and that exit writes no report.
constexpr int child_failed_exit_code = 127;

// Child side, after fork: only async-signal-safe calls, no allocation.
// Reports errno to the parent through the pipe, then exits.
[[noreturn]] void child_fail(int report_fd, int err)
{
    (void)!::write(report_fd, &err, sizeof(err));
    ::_exit(child_failed_exit_code);
}

// 0, or -1 with errno set, as the call it wraps.
int child_group(rv_editor_spawn_group group)
{
    switch (group) {
    case rv_editor_spawn_group::own:
        return ::setpgid(0, 0);
    case rv_editor_spawn_group::session:
        return ::setsid() < 0 ? -1 : 0;
    case rv_editor_spawn_group::inherit:
        break;
    }
    return 0;
}

// from == to: dup2 is a no-op that leaves the flags alone, so fcntl clears close-on-exec. Needed when
// the tty was opened O_CLOEXEC and landed on 0 already.
int child_dup(int from, int to)
{
    if (from == to) {
        return ::fcntl(to, F_SETFD, 0);
    }
    return ::dup2(from, to) < 0 ? -1 : 0;
}

int child_signals(const sigset_t &defaults)
{
    for (int sig = 1; sig < NSIG; ++sig) {
        // sigismember() is -1 for numbers the platform lacks. SIGKILL and SIGSTOP cannot be
        // caught or reset: sigaction fails on them, and posix_spawn skips them the same way.
        if (::sigismember(&defaults, sig) != 1 || sig == SIGKILL || sig == SIGSTOP) {
            continue;
        }
        struct sigaction sa{};
        sa.sa_handler = SIG_DFL;
        if (::sigaction(sig, &sa, nullptr) != 0) {
            return -1;
        }
    }
    return 0;
}

[[noreturn]] void child_run(const rv_editor_spawn_setup &setup,
    pid_t parent,
    int report_fd,
    const char *path,
    char *const argv[],
    char *const envp[])
{
#ifdef __linux__
    // Armed first: the parent's death after this point sends SIGKILL. The signal follows
    // the thread that forked, and start() runs on the UI thread, which lives as long as the editor.
    if (::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) {
        child_fail(report_fd, errno);
    }
#endif
    // The parent may have died before prctl armed the signal: then no SIGKILL will come.
    if (::getppid() != parent) {
        ::_exit(child_failed_exit_code);
    }
    if (child_group(setup.group) != 0) {
        child_fail(report_fd, errno);
    }
    if (!setup.tty.empty()) {
        // O_CLOEXEC on the temporary descriptor: the copy on stdin is not close-on-exec.
        const int tty_fd = ::open(setup.tty.c_str(), O_RDWR | O_CLOEXEC);
        if (tty_fd < 0 || child_dup(tty_fd, STDIN_FILENO) != 0) {
            child_fail(report_fd, errno);
        }
    }
    for (const auto &[from, to] : setup.dup_pairs) {
        if (child_dup(from, to) != 0) {
            child_fail(report_fd, errno);
        }
    }
    if (!setup.cwd.empty() && ::chdir(setup.cwd.c_str()) != 0) {
        child_fail(report_fd, errno);
    }
    if (child_signals(setup.signals_default) != 0) {
        child_fail(report_fd, errno);
    }
    if (setup.set_mask && ::sigprocmask(SIG_SETMASK, &setup.mask, nullptr) != 0) {
        child_fail(report_fd, errno);
    }
    ::execve(path, argv, envp);
    child_fail(report_fd, errno);
}

// Same errno mapping as the posix_spawn start() this module replaces.
int rv_editor_spawn_rc(int err)
{
    switch (err) {
    case ENOENT:
        return RV_ERR_NOENT;
    case EINVAL:
        return RV_ERR_INVAL;
    default:
        return RV_ERR_IO;
    }
}

// Kills a child whose start failed and reaps it: its report may be unreadable after it already exec'd.
// SIGKILL cannot be caught, so the wait is usually short; a child in uninterruptible sleep (D state) holds it longer.
// Reaping relies on the editor not setting SIGCHLD to SIG_IGN and not calling waitpid(-1).
// A failure is RV_ERR_IO, with the reason appended to `error`.
int rv_editor_kill_reap(pid_t pid, std::string &error)
{
    if (::kill(pid, SIGKILL) != 0) {
        error += std::string("; kill: ") + std::strerror(errno);
        return RV_ERR_IO;
    }
    while (::waitpid(pid, nullptr, 0) < 0) {
        if (errno != EINTR) {
            error += std::string("; waitpid: ") + std::strerror(errno);
            return RV_ERR_IO;
        }
    }
    return RV_OK;
}

// Names the reason of a failed start: a short or unreadable report is an I/O failure.
int rv_editor_spawn_fail(const char *path, ssize_t n, int child_errno, int read_errno, std::string &error)
{
    if (n < 0) {
        error = std::string(path) + ": exec report: " + std::strerror(read_errno);
        return RV_ERR_IO;
    }
    if (n != static_cast<ssize_t>(sizeof(child_errno))) {
        error = std::string(path) + ": exec report: incomplete";
        return RV_ERR_IO;
    }
    error = std::string(path) + ": " + std::strerror(child_errno);
    return rv_editor_spawn_rc(child_errno);
}

// Lowest number for a report end: above stdin/stdout/stderr and every dup_pairs target. The child's dup2
// onto a target would otherwise overwrite the report end, and the exec failure would be lost.
int rv_editor_report_min_fd(const rv_editor_spawn_setup &setup)
{
    int min_fd = STDERR_FILENO + 1;
    for (const auto &[from, to] : setup.dup_pairs) {
        min_fd = std::max(min_fd, to + 1);
    }
    return min_fd;
}

// Moves both report ends to numbers >= min_fd, close-on-exec kept. The original ends are always closed;
// on failure no new ends remain.
int rv_editor_report_lift(int fds[2], int min_fd, std::string &error)
{
    const int read_end = ::fcntl(fds[0], F_DUPFD_CLOEXEC, min_fd);
    const int write_end = read_end < 0 ? -1 : ::fcntl(fds[1], F_DUPFD_CLOEXEC, min_fd);
    const int lift_errno = errno;
    ::close(fds[0]);
    ::close(fds[1]);
    if (read_end < 0 || write_end < 0) {
        // close(-1) fails with EBADF, which is harmless; lift_errno is already saved.
        ::close(read_end);
        ::close(write_end);
        error = std::string("pipe: fcntl: ") + std::strerror(lift_errno);
        return RV_ERR_IO;
    }
    fds[0] = read_end;
    fds[1] = write_end;
    return RV_OK;
}

} // namespace

int rv_editor_spawn(const char *path,
    char *const argv[],
    char *const envp[],
    const rv_editor_spawn_setup &setup,
    pid_t &pid,
    std::string &error)
{
    pid = -1;
    int fds[2] = { -1, -1 };
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        error = std::string("pipe: ") + std::strerror(errno);
        return RV_ERR_IO;
    }
    if (rv_editor_report_lift(fds, rv_editor_report_min_fd(setup), error) != RV_OK) {
        return RV_ERR_IO;
    }
    const pid_t parent = ::getpid();
    const pid_t child = ::fork();
    if (child == 0) {
        child_run(setup, parent, fds[1], path, argv, envp);
    }
    if (child < 0) {
        const int fork_errno = errno;
        ::close(fds[0]);
        ::close(fds[1]);
        error = std::string("fork: ") + std::strerror(fork_errno);
        return RV_ERR_IO;
    }
    ::close(fds[1]);

    // Empty report: exec happened (the pipe closed on exec), or the child was killed by a signal
    // before exec and wrote nothing; the caller's reap shows which. Otherwise the child wrote its errno.
    int child_errno = 0;
    ssize_t n = 0;
    do {
        n = ::read(fds[0], &child_errno, sizeof(child_errno));
    } while (n < 0 && errno == EINTR);
    const int read_errno = n < 0 ? errno : 0;
    ::close(fds[0]);
    if (n != 0) {
        const int fail_rc = rv_editor_spawn_fail(path, n, child_errno, read_errno, error);
        const int reap_rc = rv_editor_kill_reap(child, error);
        return reap_rc == RV_OK ? fail_rc : RV_ERR_IO;
    }
    pid = child;
    return RV_OK;
}

} // namespace rv_editor
