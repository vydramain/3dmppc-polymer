// Pseudo-terminals on POSIX: posix_openpt, and posix_spawn into a new session
// whose controlling terminal is the PTY's other side.

#include "platform/rv_editor_pty.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <spawn.h>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "pdk/rv_err.h"

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

// Graceful shutdown timeout before SIGKILL
constexpr int graceful_shutdown_timeout_ms = 500;
// Sleep interval during graceful shutdown
constexpr int shutdown_poll_sleep_ms = 5;

// Procfs root directory containing process information
constexpr std::string_view proc_root_path = "/proc";
// Procfs stat file with process state
constexpr std::string_view proc_stat_file = "stat";
// Digit characters for matching PID directory names
constexpr std::string_view pid_digit_chars = "0123456789";
// Expected number of fields parsed from /proc/[pid]/stat after closing paren
constexpr int proc_stat_field_count = 4;
// Radix for parsing PID as decimal number
constexpr int decimal_radix = 10;

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

// Every process of session `sid` (Linux: /proc/<pid>/stat), so a job the shell
// sent to the background leaves with it. Throws nothing: stop() runs it from a
// destructor, while /proc changes under it.
std::vector<pid_t> rv_editor_session_members(pid_t sid)
{
    std::vector<pid_t> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(proc_root_path, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name.find_first_not_of(pid_digit_chars) != std::string::npos) {
            continue;
        }
        std::ifstream stat(it->path() / proc_stat_file);
        std::string line;
        std::getline(stat, line);
        // pid (comm) state ppid pgrp session ...; comm may hold spaces and ')'.
        const size_t close = line.rfind(')');
        char state = 0;
        long ppid = 0;
        long pgrp = 0;
        long session = 0;
        if (close != std::string::npos &&
            std::sscanf(line.c_str() + close + 1, " %c %ld %ld %ld", &state, &ppid, &pgrp, &session) ==
                proc_stat_field_count &&
            session == sid) {
            out.push_back(static_cast<pid_t>(std::strtol(name.c_str(), nullptr, decimal_radix)));
        }
    }
    return out;
}

void rv_editor_signal_session(pid_t sid, int sig)
{
    for (const pid_t pid : rv_editor_session_members(sid)) {
        ::kill(pid, sig);
    }
}

} // namespace

rv_editor_pty::~rv_editor_pty()
{
    stop();
}

int rv_editor_pty::start(const std::vector<std::string> &argv, const std::filesystem::path &cwd, int cols,
    int rows, std::string &error)
{
    if (argv.empty() || running()) {
        error = argv.empty() ? "nothing to run" : "already running";
        return RV_ERR_INVAL;
    }
    stop();
    pending_.clear();
    exit_ = {};
    pid_ = -1;

    const int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    char name[pty_name_buffer_size] = {};
    if (master < 0 || ::grantpt(master) != 0 || ::unlockpt(master) != 0 ||
        ::ptsname_r(master, name, sizeof(name)) != 0) {
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
    rv_editor_guard_add(pid);
    master_ = master;
    const int flags = ::fcntl(master_, F_GETFL);
    if (flags < 0 || ::fcntl(master_, F_SETFL, flags | O_NONBLOCK) < 0) {
        // A blocking terminal would stall the UI thread: this child is not kept.
        error = std::string("fcntl: ") + std::strerror(errno);
        stop();
        return RV_ERR_IO;
    }
    return RV_OK;
}

bool rv_editor_pty::read(std::string &out, size_t limit)
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
        ::close(master_);
        master_ = -1;
        pending_.clear();
    }
    return master_ >= 0;
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

bool rv_editor_pty::poll()
{
    if (pid_ <= 0 || exit_.exited) {
        return exit_.exited;
    }
    int status = 0;
    if (!rv_editor_reap(pid_, status, false)) {
        return false;
    }
    rv_editor_guard_remove(pid_);
    exit_.exited = true;
    if (WIFEXITED(status)) {
        exit_.code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_.signal = WTERMSIG(status);
    }
    // The session ends with its shell, jobs left in the background too. Only now:
    // once nothing holds the session, its id may name another process's.
    rv_editor_signal_session(pid_, SIGKILL);
    return true;
}

void rv_editor_pty::stop()
{
    if (running()) {
        // The child leads its session, whose id is its pid: a hang-up for all of
        // it, as closing a terminal window gives; SIGKILL after graceful timeout.
        rv_editor_signal_session(pid_, SIGHUP);
        rv_editor_signal_session(pid_, SIGCONT);
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(graceful_shutdown_timeout_ms);
        while (!poll() && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(shutdown_poll_sleep_ms));
        }
        if (!exit_.exited) {
            rv_editor_signal_session(pid_, SIGKILL);
            int status = 0;
            rv_editor_reap(pid_, status, true);
            rv_editor_guard_remove(pid_);
            exit_.exited = true;
            exit_.signal = SIGKILL;
        }
    }
    if (master_ >= 0) {
        ::close(master_);
        master_ = -1;
    }
    pending_.clear();
}

} // namespace rv_editor
