// Process cleanup guard: forks early and kills children when editor dies.

#include "platform/rv_editor_process.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <set>
#include <string>
#include <sys/types.h>
#include <unistd.h>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Guard protocol constants: "a<10-digit-pid>\n" or "r<10-digit-pid>\n" record format
constexpr int guard_record_size = 12;
// Offset to PID field (after command byte)
constexpr int guard_record_pid_offset = 1;
// Position of '\n' at end
constexpr int guard_record_newline_pos = guard_record_size - 1;
// Buffer for snprintf (record + null)
constexpr int guard_snprintf_buf_size = guard_record_size + 1;
// Radix for parsing PID as decimal
constexpr int decimal_radix = 10;
// Add command: register process group with guard
constexpr char guard_cmd_add = 'a';
// Remove command: unregister process group from guard
constexpr char guard_cmd_remove = 'r';
// Record end marker
constexpr char guard_record_end = '\n';
// Seconds to sleep after SIGTERM before SIGKILL
constexpr int guard_sleep_duration_sec = 1;

// Global guard state: -1 = not started.
int rv_editor_guard_guard_pid = -1;
int rv_editor_guard_write_fd = -1;

// Kill process groups: SIGTERM first (build tool stops child jobs), wait, then SIGKILL.
[[noreturn]] void rv_editor_kill_groups(const std::set<pid_t> &groups, int exit_code)
{
    for (pid_t group : groups) {
        ::kill(-group, SIGTERM);
    }
    ::sleep(guard_sleep_duration_sec);
    for (pid_t group : groups) {
        ::kill(-group, SIGKILL);
    }
    ::_exit(exit_code);
}

// Send add or remove command to guard: "a<10-digit-pid>\n" or "r<10-digit-pid>\n".
int rv_editor_guard_send(char cmd, pid_t group, std::string &error)
{
    if (rv_editor_guard_write_fd < 0) {
        error = "the process guard is not started";
        return RV_ERR_INVAL;
    }
    if (group <= 0) {
        error = "pid must be positive";
        return RV_ERR_INVAL;
    }

    char buf[guard_snprintf_buf_size];
    // Format: %010ld gives 10 digits (guard_record_newline_pos - guard_record_pid_offset)
    const int len = std::snprintf(buf, guard_snprintf_buf_size, "%c%010ld%c", cmd, static_cast<long>(group), guard_record_end);
    if (len != guard_record_size) {
        error = "record length is " + std::to_string(len);
        return RV_ERR_INVAL;
    }

    ssize_t write_result = -1;
    while ((write_result = ::write(rv_editor_guard_write_fd, buf, guard_record_size)) < 0 && errno == EINTR) {
    }
    if (write_result == guard_record_size) {
        return RV_OK;
    }
    if (write_result < 0) {
        error = errno == EPIPE ? std::string("the process guard has exited") : std::string("write: ") + std::strerror(errno);
        return RV_ERR_IO;
    }
    error = "write: short write";
    return RV_ERR_IO;
}

// Guard loop: read "a<pid>\n" and "r<pid>\n" records, kill all groups on EOF.
[[noreturn]] void rv_editor_guard_loop(int read_fd)
{
    std::set<pid_t> groups;

    while (true) {
        char buf[guard_record_size];
        const ssize_t n = ::read(read_fd, buf, sizeof(buf));

        // EOF: editor is gone, clean up and exit.
        if (n == 0) {
            rv_editor_kill_groups(groups, 0);
        }

        // A signal interrupted the read: try again.
        const bool interrupted = n < 0 && errno == EINTR;
        if (interrupted) {
            continue;
        }
        // Read error other than EINTR: assume editor crashed, clean up.
        if (n < 0) {
            rv_editor_kill_groups(groups, 1);
        }

        // Parse fixed-size record: "a<10-digit-pid>\n" or "r<10-digit-pid>\n".
        // Only channel of the guard: the editor's send() never writes a malformed record, so each skip is reported here.
        if (n != guard_record_size) {
            std::fprintf(stderr,
                "3dmppc-editor: guard skipped a malformed record: length %zd, expected %d\n",
                n,
                guard_record_size);
            continue;
        }
        if (buf[0] != guard_cmd_add && buf[0] != guard_cmd_remove) {
            std::fprintf(stderr, "3dmppc-editor: guard skipped a malformed record: unknown command byte\n");
            continue;
        }

        if (buf[guard_record_newline_pos] != guard_record_end) {
            std::fprintf(stderr, "3dmppc-editor: guard skipped a malformed record: missing record end\n");
            continue;
        }

        // strtol skips blanks and would read past the record: the field must be digits only.
        const bool digits = std::all_of(buf + guard_record_pid_offset, buf + guard_record_newline_pos, [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        });
        if (!digits) {
            std::fprintf(stderr, "3dmppc-editor: guard skipped a malformed record: pid field is not digits\n");
            continue;
        }
        const long pid_val = ::strtol(buf + guard_record_pid_offset, nullptr, decimal_radix);
        if (pid_val > std::numeric_limits<pid_t>::max()) {
            std::fprintf(stderr, "3dmppc-editor: guard skipped a malformed record: pid out of range\n");
            continue;
        }
        if (pid_val <= 0) {
            std::fprintf(stderr, "3dmppc-editor: guard skipped a malformed record: pid not positive\n");
            continue;
        }
        const pid_t pid = static_cast<pid_t>(pid_val);

        switch (buf[0]) {
        case guard_cmd_add:
            groups.insert(pid);
            break;
        case guard_cmd_remove:
            groups.erase(pid);
            break;
        }
    }
}

// Creates the pipe: fds[0] is the guard's read end, fds[1] the editor's write end.
int rv_editor_guard_make_pipe(int fds[2], std::string &error)
{
    if (::pipe(fds) != 0) {
        error = std::string("pipe: ") + std::strerror(errno);
        return RV_ERR_IO;
    }
    return RV_OK;
}

// Forks the guard, which runs its loop until the write end closes. Takes both ends:
// closes them on failure, and the read end on success.
int rv_editor_guard_spawn(int fds[2], pid_t &pid, std::string &error)
{
    pid = ::fork();
    if (pid < 0) {
        error = std::string("fork: ") + std::strerror(errno);
        ::close(fds[0]);
        ::close(fds[1]);
        return RV_ERR_IO;
    }

    if (pid == 0) {
        // Guard process: close write end, ignore signals, start loop.
        ::close(fds[1]);

        // Ignore signals sent to the terminal session.
        std::signal(SIGINT, SIG_IGN);
        std::signal(SIGQUIT, SIG_IGN);
        std::signal(SIGHUP, SIG_IGN);
        std::signal(SIGTERM, SIG_IGN);
        std::signal(SIGPIPE, SIG_IGN);

        rv_editor_guard_loop(fds[0]);
    }

    ::close(fds[0]);
    return RV_OK;
}

// Editor side of the pipe: a dead guard must give EPIPE, not kill the editor with
// SIGPIPE, and the write end must not leak into children. Takes the write end: closes it on failure.
int rv_editor_guard_prepare_editor(int write_fd, std::string &error)
{
    std::signal(SIGPIPE, SIG_IGN);
    if (::fcntl(write_fd, F_SETFD, FD_CLOEXEC) < 0) {
        error = std::string("fcntl: ") + std::strerror(errno);
        ::close(write_fd);
        return RV_ERR_IO;
    }
    return RV_OK;
}

} // namespace

// Start the guard: forks, parent returns, guard loop runs in child.
// Call once from main() before SDL_Init. RV_OK, or RV_ERR_* with the reason in `error`.
int rv_editor_guard_start(std::string &error)
{
    if (rv_editor_guard_guard_pid != -1) {
        error = "the process guard is already running";
        return RV_ERR_BUSY;
    }

    int fds[2] = { -1, -1 };
    int rc = rv_editor_guard_make_pipe(fds, error);
    if (rc != RV_OK) {
        return rc;
    }

    pid_t pid = -1;
    rc = rv_editor_guard_spawn(fds, pid, error);
    if (rc != RV_OK) {
        return rc;
    }

    rc = rv_editor_guard_prepare_editor(fds[1], error);
    if (rc != RV_OK) {
        return rc;
    }

    rv_editor_guard_guard_pid = pid;
    rv_editor_guard_write_fd = fds[1];
    return RV_OK;
}

// Register a process group with the guard. RV_OK, or RV_ERR_* with the reason in `error`.
int rv_editor_guard_add(pid_t group, std::string &error)
{
    return rv_editor_guard_send(guard_cmd_add, group, error);
}

// Unregister a process group from the guard. RV_OK, or RV_ERR_* with the reason in `error`.
int rv_editor_guard_remove(pid_t group, std::string &error)
{
    return rv_editor_guard_send(guard_cmd_remove, group, error);
}

} // namespace rv_editor
