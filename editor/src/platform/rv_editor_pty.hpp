#pragma once

#include <chrono>
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
    // Hangs up a child still running, without waiting: the process guard ends it at editor exit.
    ~rv_editor_pty();

    // argv[0] is run as is (no PATH search) in `cwd`, on a terminal of cols x rows,
    // with TERM naming the emulator. RV_OK on success; RV_ERR_INVAL for invalid call,
    // RV_ERR_NOENT if program not found, RV_ERR_IO for OS failures. Reason in `error`.
    int start(const std::vector<std::string> &argv, const std::filesystem::path &cwd, int cols, int rows, std::string &error);

    bool running() const
    {
        return pid_ > 0 && !exit_.exited;
    }
    const rv_editor_process::rv_editor_exit &exit_status() const
    {
        return exit_;
    }

    // Appends what the child wrote, at most `limit` bytes. RV_OK at the normal end
    // (EIO or end of file: nothing holds the other side any more); RV_ERR_IO with the
    // reason in `error` for any other failure. Either way the terminal is closed.
    int read(std::string &out, size_t limit, std::string &error);

    // Keys typed ahead of a child that is not reading: past this they are refused.
    static constexpr size_t input_max = 1 << 16;
    // Queues keys and writes as much as the terminal takes; the rest goes on the
    // next flush. RV_OK if queued; RV_ERR_BUSY if input_max exceeded, RV_ERR_IO if closed.
    int write(std::string_view bytes);
    void flush();

    // The terminal's size; the kernel tells the child with SIGWINCH.
    void resize(int cols, int rows);

    // Reaps the child once it has ended and drops its guard. RV_OK while it runs too;
    // RV_ERR_IO with the reasons in `error` if the guard or the session refused.
    int poll(std::string &error);

    // Starts a stop: SIGHUP to the child's session, as closing a terminal window does,
    // and a deadline for SIGKILL. Returns at once. RV_OK, or RV_ERR_IO with the reason in `error`.
    int begin_stop(std::string &error);
    bool stopping() const
    {
        return stopping_;
    }
    // One step of a stop, without waiting: reaps what ended, SIGKILL once past the deadline,
    // closes the terminal once the child is gone. Called each frame while stopping().
    int stop_step(std::string &error);
    // Stops now and waits for the exit, at most the stop's deadline, then SIGKILL. For the
    // editor's exit only, where no frame steps a stop. RV_OK, or RV_ERR_IO with the reasons in `error`.
    int stop_now(std::string &error);

private:
    // Records how the child ended and drops its guard and session. RV_OK, or RV_ERR_IO.
    int record_exit(int status, std::string &error);

    pid_t pid_ = -1;
    int master_ = -1;
    bool stopping_ = false;
    bool stop_failure_reported_ = false; // a failing stop step is reported once per stop
    bool kill_sent_ = false;             // SIGKILL past the deadline is sent once per stop
    std::chrono::steady_clock::time_point stop_until_{};
    std::string pending_;
    rv_editor_process::rv_editor_exit exit_{};
};

} // namespace rv_editor
