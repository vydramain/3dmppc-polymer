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
    ::sleep(1);
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
        char buf[12];
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
        if (n != 12 || (buf[0] != 'a' && buf[0] != 'r')) {
            continue;
        }

        char *end = nullptr;
        const long pid_val = ::strtol(buf + 1, &end, 10);
        if (end != buf + 11 || buf[11] != '\n') {
            continue;
        }
        const pid_t pid = static_cast<pid_t>(pid_val);

        if (buf[0] == 'a') {
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

    char buf[16];
    const int len = std::snprintf(buf, 13, "a%010ld\n", static_cast<long>(group));
    if (len != 12) {
        return;
    }

    if (::write(rv_editor_guard_write_fd, buf, 12) != 12) {
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

    char buf[16];
    const int len = std::snprintf(buf, 13, "r%010ld\n", static_cast<long>(group));
    if (len != 12) {
        return;
    }

    if (::write(rv_editor_guard_write_fd, buf, 12) != 12) {
        if (errno != EPIPE && !rv_editor_guard_write_error_printed) {
            std::fprintf(stderr, "3dmppc-editor: guard write failed: %s\n", std::strerror(errno));
            rv_editor_guard_write_error_printed = true;
        }
    }
}

} // namespace rv_editor
