#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

#include "platform/rv_editor_process.hpp"

namespace rv_editor
{

// A program on a pseudo-terminal: the child leads a
// session of its own whose controlling terminal is the PTY, so Ctrl+C and a resize
// reach whatever runs in it. It gets no pipe of the editor's and never sees the dev
// protocol. Reading and writing never block; the UI thread drains once a
// frame.
class rv_editor_pty
{
public:
    rv_editor_pty() = default;
    rv_editor_pty(const rv_editor_pty &) = delete;
    rv_editor_pty &operator=(const rv_editor_pty &) = delete;
    // Hangs up and reaps a child still running: the editor leaves no orphan.
    ~rv_editor_pty();

    // argv[0] is run as is (no PATH search) in `cwd`, on a terminal of cols x rows,
    // with TERM naming the emulator. RV_OK on success; RV_ERR_INVAL for invalid call,
    // RV_ERR_NOENT if program not found, RV_ERR_IO for OS failures. Reason in `error`.
    int start(const std::vector<std::string> &argv, const std::filesystem::path &cwd, int cols, int rows,
        std::string &error);

    bool running() const { return pid_ > 0 && !exit_.exited; }
    const rv_editor_process::rv_editor_exit &exit_status() const { return exit_; }

    // Appends what the child wrote, at most `limit` bytes. False once the terminal
    // is closed: nothing in the session holds it any more.
    bool read(std::string &out, size_t limit);

    // Keys typed ahead of a child that is not reading: past this they are refused.
    static constexpr size_t input_max = 1 << 16;
    // Queues keys and writes as much as the terminal takes; the rest goes on the
    // next flush. RV_OK if queued; RV_ERR_BUSY if input_max exceeded, RV_ERR_IO if closed.
    int write(std::string_view bytes);
    void flush();

    // The terminal's size; the kernel tells the child with SIGWINCH.
    void resize(int cols, int rows);

    // Reaps the child when it has ended. Returns true when it is (now) ended.
    bool poll();

    // SIGHUP to the child's session, as closing a terminal window does, then
    // SIGKILL for whatever ignored it; the child is reaped before this returns.
    void stop();

private:
    pid_t pid_ = -1;
    int master_ = -1;
    std::string pending_;
    rv_editor_process::rv_editor_exit exit_{};
};

} // namespace rv_editor
