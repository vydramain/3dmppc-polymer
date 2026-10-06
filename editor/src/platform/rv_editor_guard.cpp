// Process cleanup guard: forks early and kills children when editor dies.

#include "platform/rv_editor_process.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <sys/types.h>
#include <unistd.h>

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

// Global guard state: -1 = not started, 0 = guard process, >0 = editor process.
int rv_editor_guard_guard_pid = -1;
int rv_editor_guard_write_fd = -1;
bool rv_editor_guard_failed = false;
bool rv_editor_guard_write_error_printed = false;

// Kill process groups: SIGTERM first (build tool stops child jobs), wait, then SIGKILL.
void rv_editor_kill_groups(const std::set<pid_t> &groups, int exit_code)
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

// Guard loop: read "a<pid>\n" and "r<pid>\n" records, kill all groups on EOF.
void rv_editor_guard_loop(int read_fd)
{
    std::set<pid_t> groups;

    while (true) {
        char buf[guard_record_size];
        const ssize_t n = ::read(read_fd, buf, sizeof(buf));

        // EOF: editor is gone, clean up and exit.
        if (n == 0) {
            rv_editor_kill_groups(groups, 0);
        }

        // Read error other than EINTR: assume editor crashed, clean up.
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            rv_editor_kill_groups(groups, 1);
        }

        // Parse fixed-size record: "a<10-digit-pid>\n" or "r<10-digit-pid>\n".
        if (n != guard_record_size || (buf[0] != guard_cmd_add && buf[0] != guard_cmd_remove)) {
            continue;
        }

        char *end = nullptr;
        const long pid_val = ::strtol(buf + guard_record_pid_offset, &end, decimal_radix);
        if (end != buf + guard_record_newline_pos || buf[guard_record_newline_pos] != guard_record_end) {
            continue;
        }
        const pid_t pid = static_cast<pid_t>(pid_val);

        if (buf[0] == guard_cmd_add) {
            groups.insert(pid);
        } else {
            groups.erase(pid);
        }
    }
}

} // namespace

// Start the guard: forks, parent returns, guard loop runs in child.
// Call once from main() before SDL_Init. On failure, prints once and continues.
void rv_editor_guard_start()
{
    if (rv_editor_guard_guard_pid != -1) {
        return;
    }

    int fds[2] = { -1, -1 };
    if (::pipe(fds) != 0) {
        std::fprintf(stderr, "3dmppc-editor: guard pipe failed: %s\n", std::strerror(errno));
        rv_editor_guard_failed = true;
        return;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        std::fprintf(stderr, "3dmppc-editor: guard fork failed: %s\n", std::strerror(errno));
        ::close(fds[0]);
        ::close(fds[1]);
        rv_editor_guard_failed = true;
        return;
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
        ::_exit(1);
    }

    // Editor process: close read end, set close-on-exec on write end.
    ::close(fds[0]);
    if (::fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0) {
        std::fprintf(stderr, "3dmppc-editor: guard fcntl failed: %s\n", std::strerror(errno));
        ::close(fds[1]);
        rv_editor_guard_failed = true;
        return;
    }

    rv_editor_guard_guard_pid = pid;
    rv_editor_guard_write_fd = fds[1];
}

// Register a process group with the guard.
void rv_editor_guard_add(pid_t group)
{
    if (rv_editor_guard_failed || rv_editor_guard_write_fd < 0) {
        return;
    }

    char buf[guard_snprintf_buf_size];
    // Format: %010ld gives 10 digits (guard_record_newline_pos - guard_record_pid_offset)
    const int len =
        std::snprintf(buf, guard_snprintf_buf_size, "%c%010ld%c", guard_cmd_add, static_cast<long>(group), guard_record_end);
    if (len != guard_record_size) {
        return;
    }

    if (::write(rv_editor_guard_write_fd, buf, guard_record_size) != guard_record_size) {
        if (errno != EPIPE && !rv_editor_guard_write_error_printed) {
            std::fprintf(stderr, "3dmppc-editor: guard write failed: %s\n", std::strerror(errno));
            rv_editor_guard_write_error_printed = true;
        }
    }
}

// Unregister a process group from the guard.
void rv_editor_guard_remove(pid_t group)
{
    if (rv_editor_guard_failed || rv_editor_guard_write_fd < 0) {
        return;
    }

    char buf[guard_snprintf_buf_size];
    // Format: %010ld gives 10 digits (guard_record_newline_pos - guard_record_pid_offset)
    const int len =
        std::snprintf(buf, guard_snprintf_buf_size, "%c%010ld%c", guard_cmd_remove, static_cast<long>(group), guard_record_end);
    if (len != guard_record_size) {
        return;
    }

    if (::write(rv_editor_guard_write_fd, buf, guard_record_size) != guard_record_size) {
        if (errno != EPIPE && !rv_editor_guard_write_error_printed) {
            std::fprintf(stderr, "3dmppc-editor: guard write failed: %s\n", std::strerror(errno));
            rv_editor_guard_write_error_printed = true;
        }
    }
}

} // namespace rv_editor
