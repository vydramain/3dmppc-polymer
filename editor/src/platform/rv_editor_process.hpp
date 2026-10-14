#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_editor
{

// A child process the editor owns: argv, cwd and
// environment as separate values, never a shell line, and its own
// stdin/stdout/stderr pipes. Reading never blocks: the UI thread drains the pipes
// once a frame. The child leads its own process group, so stopping it
// also stops whatever it started (a compiler under the burner).
class rv_editor_process
{
public:
    // How the child ended.
    struct rv_editor_exit {
        bool exited = false;      // false while it runs
        int code = 0;             // exit status, when it exited normally
        int signal = 0;           // non-zero when a signal ended it
        int guard_result = RV_OK; // rv_editor_guard_remove code, after the exit
        std::string guard_error;  // its reason, empty when fine
    };

    rv_editor_process() = default;
    rv_editor_process(const rv_editor_process &) = delete;
    rv_editor_process &operator=(const rv_editor_process &) = delete;
    // Kills and reaps a child still running: the editor leaves no orphan.
    ~rv_editor_process();

    // argv[0] is the executable path, run as is (no PATH search). `inherit_fd`,
    // when not -1, reaches the child as its descriptor 3; `env` (KEY=VALUE) goes over
    // the editor's environment. RV_OK on success, RV_ERR_* with the reason in `error`.
    int start(const std::vector<std::string> &argv,
        const std::filesystem::path &cwd,
        std::string &error,
        int inherit_fd = -1,
        const std::vector<std::string> &env = {});

    bool running() const
    {
        return pid_ > 0 && !exit_.exited;
    }
    pid_t pid() const
    {
        return pid_;
    }
    // False once the child's stdout reached end of file or a read failure reported by read().
    bool stdout_open() const
    {
        return out_ >= 0;
    }
    // For a caller that reads on a thread of its own instead of read().
    int stdout_fd() const
    {
        return out_;
    }
    int stderr_fd() const
    {
        return err_;
    }
    // End of input for the child.
    void close_stdin();
    // False once stdin is closed: the child is gone or stopped reading for good.
    bool stdin_open() const
    {
        return in_ >= 0;
    }
    const rv_editor_exit &exit_status() const
    {
        return exit_;
    }

    // Appends whatever the pipes hold now, at most `limit` bytes each. RV_OK - no read
    // failed; the rest waits for the next call. RV_ERR_IO - a read failed, the reason is
    // in `error`, that pipe is closed. End of output is still output_done().
    int read(std::string &out, std::string &err, size_t limit, std::string &error);

    // What stdin may hold unwritten: past it the child is not reading.
    static constexpr size_t input_max = 1 << 20;

    // Queues bytes for stdin and writes as much as the pipe takes; the rest goes
    // on the next flush. Whole or nothing, so a message is never cut: RV_OK on success,
    // RV_ERR_BUSY when the bytes would take the queue past input_max, RV_ERR_IO when
    // stdin is closed or when the child is gone. Never waits for the child.
    int write(std::string_view bytes);
    void flush();

    // Reaps the child when it has ended; the guard's remove code and reason come with the
    // exit status. Returns true when it is (now) ended.
    bool poll();

    // True once both pipes are closed (end of file or a read failure reported by read()),
    // or once the 2 s grace after the child exited has passed: past this point no more
    // of its output is coming.
    // When the grace ends first, closes the pipes itself and remembers the cut.
    // Only meaningful after poll() returned true.
    bool output_done();
    // True once output_done() cut the pipes short of end of file.
    bool output_cut() const
    {
        return cut_;
    }

    // SIGTERM, or SIGKILL with `force`, to the child's process group. RV_OK on success,
    // RV_ERR_ALREADY when the leader is already reaped (the group is not signalled),
    // RV_ERR_IO if signal delivery failed.
    int stop(bool force);

private:
    void close_fds();

    pid_t pid_ = -1;
    int in_ = -1;
    int out_ = -1;
    int err_ = -1;
    std::string pending_;
    rv_editor_exit exit_{};
    std::chrono::steady_clock::time_point exited_at_{};
    bool cut_ = false;
};

// Human-readable reason an exit happened: "exit code 2", "killed by SIGSEGV".
std::string rv_editor_exit_text(const rv_editor_process::rv_editor_exit &exit);

// The first executable `name` on PATH, or empty: for the user's own tools (nvim, git).
std::filesystem::path rv_editor_process_find(const char *name);

// Starts argv[0] as is (no PATH search), not waited for and not in the editor's group, so the editor's
// stop and exit leave it alone. Double-forked with setsid: no zombie, and a desktop helper (xdg-open)
// may keep its browser alive. RV_OK, or RV_ERR_* with the reason in `error`.
int rv_editor_launch_detached(const std::vector<std::string> &argv, std::string &error);

// Guard process that kills all children on editor exit. Call once from main() before SDL_Init.
// RV_OK, or RV_ERR_* with the reason in `error`.
int rv_editor_guard_start(std::string &error);

// Register a process group with the guard; unregistered groups survive the editor's death.
// RV_OK, or RV_ERR_* with the reason in `error`.
int rv_editor_guard_add(pid_t group, std::string &error);

// Unregister a process group from the guard. RV_OK, or RV_ERR_* with the reason in `error`.
int rv_editor_guard_remove(pid_t group, std::string &error);

} // namespace rv_editor
