#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace rv_editor
{

// A child process the editor owns (docs/adr/0012-platforms.md): argv, cwd and
// environment as separate values, never a shell line (BLD-07), and its own
// stdin/stdout/stderr pipes. Reading never blocks: the UI thread drains the pipes
// once a frame (DEV-05). The child leads its own process group, so stopping it
// also stops whatever it started (a compiler under the burner).
class rv_editor_process
{
public:
    // How the child ended.
    struct rv_editor_exit
    {
        bool exited = false; // false while it runs
        int code = 0;        // exit status, when it exited normally
        int signal = 0;      // non-zero when a signal ended it
    };

    rv_editor_process() = default;
    rv_editor_process(const rv_editor_process &) = delete;
    rv_editor_process &operator=(const rv_editor_process &) = delete;
    // Kills and reaps a child still running: the editor leaves no orphan.
    ~rv_editor_process();

    // argv[0] is the executable path, run as is (no PATH search). `inherit_fd`,
    // when not -1, reaches the child as its descriptor 3. False with the reason in
    // `error` when it cannot start.
    bool start(const std::vector<std::string> &argv, const std::filesystem::path &cwd, std::string &error,
        int inherit_fd = -1);

    bool running() const { return pid_ > 0 && !exit_.exited; }
    pid_t pid() const { return pid_; }
    // False once the child's stdout reached end of file.
    bool stdout_open() const { return out_ >= 0; }
    // For a caller that reads on a thread of its own instead of read().
    int stdout_fd() const { return out_; }
    int stderr_fd() const { return err_; }
    // End of input for the child.
    void close_stdin();
    const rv_editor_exit &exit_status() const { return exit_; }

    // Appends whatever the pipes hold now, at most `limit` bytes each. Returns
    // false once both pipes reached end of file.
    bool read(std::string &out, std::string &err, size_t limit);

    // Queues bytes for stdin and writes as much as the pipe takes; the rest goes
    // on the next flush. False when stdin is closed or the child is gone.
    bool write(std::string_view bytes);
    void flush();

    // Reaps the child when it has ended. Returns true when it is (now) ended.
    bool poll();

    // SIGTERM, or SIGKILL with `force`, to the child's process group.
    void stop(bool force);

private:
    void close_fds();

    pid_t pid_ = -1;
    int in_ = -1;
    int out_ = -1;
    int err_ = -1;
    std::string pending_;
    rv_editor_exit exit_{};
};

// Human-readable reason an exit happened: "exit code 2", "killed by SIGSEGV".
std::string rv_editor_exit_text(const rv_editor_process::rv_editor_exit &exit);

} // namespace rv_editor
