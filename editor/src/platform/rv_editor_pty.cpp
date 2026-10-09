// Pseudo-terminals on POSIX: posix_openpt, and posix_spawn into a new session
// whose controlling terminal is the PTY's other side.

#include "platform/rv_editor_pty.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "pdk/rv_err.h"
#include "platform/rv_editor_pty_session.hpp"

extern char **environ;

namespace rv_editor
{

namespace
{

// Terminal maximum size in characters (unsigned short limit)
constexpr int terminal_max_size = 0xffff;

// Environment variable prefixes to filter out when setting child's environment
constexpr std::string_view term_env_prefix = "TERM=";
constexpr std::string_view columns_env_prefix = "COLUMNS=";
constexpr std::string_view lines_env_prefix = "LINES=";

// Terminal type value for child process
constexpr std::string_view terminal_type_value = "xterm-256color";

// PTY name buffer size for ptsname_r
constexpr size_t pty_name_buffer_size = 128;

// Read buffer size for one read() from PTY per pass
constexpr size_t pty_read_buffer_size = 16384;

// Time a shell has to exit after the hang-up, before SIGKILL: long enough for a shell
// to act on HUP, short enough not to hold a frame or the editor's exit.
constexpr int graceful_shutdown_timeout_ms = 500;
// Sleep between polls of a shell at the editor's exit (stop_now); no frame runs then.
constexpr int shutdown_poll_sleep_ms = 5;

winsize rv_editor_winsize(int cols, int rows)
{
    winsize ws{};
    ws.ws_col = static_cast<unsigned short>(std::clamp(cols, 1, terminal_max_size));
    ws.ws_row = static_cast<unsigned short>(std::clamp(rows, 1, terminal_max_size));
    return ws;
}

// The editor's environment for the child, with TERM naming what draws it.
std::vector<std::string> rv_editor_pty_env()
{
    std::vector<std::string> env;
    for (char **e = environ; *e != nullptr; ++e) {
        if (std::strncmp(*e, term_env_prefix.data(), term_env_prefix.size()) != 0 &&
            std::strncmp(*e, columns_env_prefix.data(), columns_env_prefix.size()) != 0 &&
            std::strncmp(*e, lines_env_prefix.data(), lines_env_prefix.size()) != 0) {
            env.emplace_back(*e);
        }
    }
    env.emplace_back(std::string(term_env_prefix) + std::string(terminal_type_value));
    return env;
}

bool rv_editor_reap(pid_t pid, int &status, bool wait)
{
    for (;;) {
        const pid_t r = ::waitpid(pid, &status, wait ? 0 : WNOHANG);
        if (r < 0 && errno == EINTR) {
            continue;
        }
        return r == pid;
    }
}

// The leader directly, then its session: a /proc failure does not keep the leader from the signal.
int rv_editor_signal_pty(pid_t leader, int sig, std::string &error)
{
    int rc = RV_OK;
    if (::kill(leader, sig) != 0 && errno != ESRCH) {
        error = std::string("signal: ") + std::strerror(errno);
        rc = RV_ERR_IO;
    }
    std::string session_error;
    if (rv_editor_signal_session(leader, sig, session_error) != RV_OK) {
        rv_editor_add_reason(error, session_error);
        rc = RV_ERR_IO;
    }
    return rc;
}

// Ends a fresh child: its leader SIGKILLed, then its session, then the leader reaped into
// `status`. `reaped` is false when the leader kill failed: the child is still running.
int rv_editor_kill_reap(pid_t pid, int &status, bool &reaped, std::string &error)
{
    reaped = false;
    if (::kill(pid, SIGKILL) != 0) {
        error = std::string("kill: ") + std::strerror(errno);
        return RV_ERR_IO;
    }
    const int rc = rv_editor_signal_session(pid, SIGKILL, error);
    rv_editor_reap(pid, status, true);
    reaped = true;
    return rc;
}

} // namespace

rv_editor_pty::~rv_editor_pty()
{
    // Nothing waits in a destructor. A shell that ignores the hang-up is ended by the
    // process guard once the editor exits.
    std::string error;
    begin_stop(error);
    if (master_ >= 0) {
        ::close(master_);
    }
}

int rv_editor_pty::start(const std::vector<std::string> &argv,
    const std::filesystem::path &cwd,
    int cols,
    int rows,
    std::string &error)
{
    if (argv.empty() || running()) {
        error = argv.empty() ? "nothing to run" : "already running";
        return RV_ERR_INVAL;
    }
    if (master_ >= 0) {
        ::close(master_);
        master_ = -1;
    }
    pending_.clear();
    stopping_ = false;
    exit_ = {};
    pid_ = -1;

    const int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    char name[pty_name_buffer_size] = {};
    if (master < 0 || ::grantpt(master) != 0 || ::unlockpt(master) != 0 || ::ptsname_r(master, name, sizeof(name)) != 0) {
        error = std::string("pseudo-terminal: ") + std::strerror(errno);
        if (master >= 0) {
            ::close(master);
        }
        return RV_ERR_IO;
    }
    const winsize ws = rv_editor_winsize(cols, rows);
    ::ioctl(master, TIOCSWINSZ, &ws);

    // posix_spawn makes the new session before the file actions run, and the
    // first terminal a session leader opens becomes its controlling terminal.
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, name, O_RDWR, 0);
    posix_spawn_file_actions_adddup2(&actions, STDIN_FILENO, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, STDIN_FILENO, STDERR_FILENO);
    if (!cwd.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    }
    // Every signal back to its default and none blocked: a shell has to see the
    // Ctrl+C its terminal turns into SIGINT, even when the editor itself was
    // started with SIGINT ignored (a background job of a non-interactive shell).
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    sigset_t all;
    sigfillset(&all);
    posix_spawnattr_setsigdefault(&attr, &all);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);

    std::vector<char *> args;
    for (const std::string &a : argv) {
        args.push_back(const_cast<char *>(a.c_str()));
    }
    args.push_back(nullptr);
    std::vector<std::string> env = rv_editor_pty_env();
    std::vector<char *> envp;
    for (std::string &e : env) {
        envp.push_back(e.data());
    }
    envp.push_back(nullptr);

    pid_t pid = -1;
    const int rc = posix_spawn(&pid, argv[0].c_str(), &actions, &attr, args.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        ::close(master);
        error = argv[0] + ": " + std::strerror(rc);
        if (rc == ENOENT) {
            return RV_ERR_NOENT;
        }
        return RV_ERR_IO;
    }
    pid_ = pid;
    std::string guard_error;
    if (rv_editor_guard_add(pid, guard_error) != RV_OK) {
        // The guard refused: the child is killed here. If the kill fails, the child stays and the error says so.
        int status = 0;
        bool reaped = false;
        std::string kill_error;
        const int kill_rc = rv_editor_kill_reap(pid, status, reaped, kill_error);
        pid_ = -1;
        ::close(master);
        error = "process guard: " + guard_error;
        if (kill_rc != RV_OK) {
            rv_editor_add_reason(error, kill_error);
        }
        return RV_ERR_IO;
    }
    const int flags = ::fcntl(master, F_GETFL);
    if (flags < 0 || ::fcntl(master, F_SETFL, flags | O_NONBLOCK) < 0) {
        // A blocking terminal would stall the UI thread: this child is not kept.
        error = std::string("fcntl: ") + std::strerror(errno);
        int status = 0;
        bool reaped = false;
        std::string kill_error;
        const int kill_rc = rv_editor_kill_reap(pid, status, reaped, kill_error);
        // A reaped leader's pid may be reused, so the guard forgets the group. Otherwise
        // the guard keeps it and ends the session at the editor's exit.
        if (reaped) {
            std::string remove_error;
            if (rv_editor_guard_remove(pid, remove_error) != RV_OK) {
                rv_editor_add_reason(error, "process guard: " + remove_error);
            }
        }
        if (kill_rc != RV_OK) {
            rv_editor_add_reason(error, kill_error);
        }
        pid_ = -1;
        ::close(master);
        return RV_ERR_IO;
    }
    master_ = master;
    return RV_OK;
}

int rv_editor_pty::read(std::string &out, size_t limit, std::string &error)
{
    char buf[pty_read_buffer_size];
    size_t taken = 0;
    while (master_ >= 0 && taken < limit) {
        const ssize_t n = ::read(master_, buf, std::min(sizeof(buf), limit - taken));
        if (n > 0) {
            out.append(buf, static_cast<size_t>(n));
            taken += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && errno == EAGAIN) {
            break;
        }
        // EIO (Linux) or end of file: no process holds the terminal's other side.
        const int cause = n < 0 ? errno : 0;
        ::close(master_);
        master_ = -1;
        pending_.clear();
        if (n == 0 || cause == EIO) {
            return RV_OK;
        }
        error = std::string("read: ") + std::strerror(cause);
        return RV_ERR_IO;
    }
    return RV_OK;
}

int rv_editor_pty::write(std::string_view bytes)
{
    if (master_ < 0) {
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
    return master_ >= 0 ? RV_OK : RV_ERR_IO;
}

void rv_editor_pty::flush()
{
    while (master_ >= 0 && !pending_.empty()) {
        const ssize_t n = ::write(master_, pending_.data(), pending_.size());
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
        pending_.clear(); // EIO: nobody reads the terminal any more
    }
}

void rv_editor_pty::resize(int cols, int rows)
{
    if (master_ >= 0) {
        const winsize ws = rv_editor_winsize(cols, rows);
        ::ioctl(master_, TIOCSWINSZ, &ws);
    }
}

int rv_editor_pty::record_exit(int status, std::string &error)
{
    exit_.exited = true;
    if (WIFEXITED(status)) {
        exit_.code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_.signal = WTERMSIG(status);
    }
    // The session ends with its shell, jobs left in the background too. Only now:
    // once nothing holds the session, its id may name another process's.
    std::string guard_error;
    const int guard_rc = rv_editor_guard_remove(pid_, guard_error);
    exit_.guard_result = guard_rc;
    exit_.guard_error = guard_rc != RV_OK ? guard_error : std::string();
    std::string kill_error;
    const int kill_rc = rv_editor_signal_session(pid_, SIGKILL, kill_error);
    if (guard_rc != RV_OK) {
        rv_editor_add_reason(error, "process guard: " + guard_error);
    }
    if (kill_rc != RV_OK) {
        rv_editor_add_reason(error, kill_error);
    }
    return guard_rc != RV_OK || kill_rc != RV_OK ? RV_ERR_IO : RV_OK;
}

int rv_editor_pty::poll(std::string &error)
{
    if (pid_ <= 0 || exit_.exited) {
        return RV_OK;
    }
    int status = 0;
    if (!rv_editor_reap(pid_, status, false)) {
        return RV_OK;
    }
    return record_exit(status, error);
}

int rv_editor_pty::begin_stop(std::string &error)
{
    if (!running() || stopping_) {
        return RV_OK;
    }
    stopping_ = true;
    stop_failure_reported_ = false;
    kill_sent_ = false;
    stop_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(graceful_shutdown_timeout_ms);
    // The child leads its session, whose id is its pid: a hang-up for all of it.
    std::string cont_error;
    const int hup_rc = rv_editor_signal_pty(pid_, SIGHUP, error);
    const int cont_rc = rv_editor_signal_pty(pid_, SIGCONT, cont_error);
    if (cont_rc != RV_OK) {
        rv_editor_add_reason(error, cont_error);
    }
    return hup_rc == RV_OK && cont_rc == RV_OK ? RV_OK : RV_ERR_IO;
}

int rv_editor_pty::stop_step(std::string &error)
{
    if (!stopping_) {
        return RV_OK;
    }
    int rc = poll(error);
    if (running() && !kill_sent_ && std::chrono::steady_clock::now() >= stop_until_) {
        // The shell ignored the hang-up: SIGKILL to the leader, then its session. Retried
        // next frame until the leader kill goes through.
        if (::kill(pid_, SIGKILL) == 0) {
            kill_sent_ = true;
            std::string kill_error;
            if (rv_editor_signal_session(pid_, SIGKILL, kill_error) != RV_OK) {
                rv_editor_add_reason(error, kill_error);
                rc = RV_ERR_IO;
            }
        } else {
            rv_editor_add_reason(error, std::string("kill: ") + std::strerror(errno));
            rc = RV_ERR_IO;
        }
    }
    if (!running()) {
        if (master_ >= 0) {
            ::close(master_);
            master_ = -1;
        }
        pending_.clear();
        stopping_ = false;
    }
    if (rc == RV_OK || stop_failure_reported_) {
        return RV_OK;
    }
    // The failure persists until the stop ends: reported once, not on every frame.
    stop_failure_reported_ = true;
    return rc;
}

int rv_editor_pty::stop_now(std::string &error)
{
    int rc = begin_stop(error);
    // The editor's exit: no frame steps a stop here, so the wait is a short sleep.
    while (running() && std::chrono::steady_clock::now() < stop_until_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(shutdown_poll_sleep_ms));
        std::string poll_error;
        if (poll(poll_error) != RV_OK) {
            rv_editor_add_reason(error, poll_error);
            rc = RV_ERR_IO;
        }
    }
    if (running()) {
        // Past the deadline: SIGKILL, then a reap that does not wait. A child not reaped
        // yet stays with the process guard, which ends it at the editor's exit.
        if (::kill(pid_, SIGKILL) != 0) {
            rv_editor_add_reason(error, std::string("kill: ") + std::strerror(errno));
            rc = RV_ERR_IO;
        }
        std::string kill_error;
        if (rv_editor_signal_session(pid_, SIGKILL, kill_error) != RV_OK) {
            rv_editor_add_reason(error, kill_error);
            rc = RV_ERR_IO;
        }
        int status = 0;
        pid_t r = -1;
        do {
            r = ::waitpid(pid_, &status, WNOHANG);
        } while (r < 0 && errno == EINTR);
        if (r < 0) {
            rv_editor_add_reason(error, std::string("reap: ") + std::strerror(errno));
            rc = RV_ERR_IO;
        } else if (r == pid_) {
            std::string exit_error;
            if (record_exit(status, exit_error) != RV_OK) {
                rv_editor_add_reason(error, exit_error);
                rc = RV_ERR_IO;
            }
        } else {
            rv_editor_add_reason(error, "not reaped after SIGKILL; left to the process guard");
            rc = RV_ERR_IO;
        }
    }
    if (master_ >= 0) {
        ::close(master_);
        master_ = -1;
    }
    pending_.clear();
    // Still running (not reaped): stays stopping, so the destructor does not hang up again.
    stopping_ = running();
    return rc;
}

} // namespace rv_editor
