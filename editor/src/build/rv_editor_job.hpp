#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "log/rv_editor_log.hpp"
#include "platform/rv_editor_process.hpp"

namespace rv_editor
{

// A background process the editor runs and pumps once a frame: its output goes
// to the log, a cancel stops it with SIGTERM and SIGKILL after a grace.
class rv_editor_job
{
public:
    // RV_OK if the process starts, or an rv_err code with the reason in `error`. `name` is the
    // process's word in the job's messages, e.g. "burner". `source` tags its log lines; `run` is the
    // number of this run (a build's or an image's number), 0 when there is none.
    int start(const std::vector<std::string> &argv,
        const std::filesystem::path &cwd,
        const std::string &name,
        rv_editor_log_source source,
        uint32_t run,
        std::string &error);
    // Begins the stop of a process that is not ended and not yet cancelled: SIGTERM now, while it still runs,
    // SIGKILL from pump() after the grace. RV_ERR_ALREADY with the reason in `error` otherwise.
    // RV_ERR_IO: SIGTERM was not delivered; the cancel still goes on, the reason is in `error`.
    int cancel(std::string &error);
    // Once a frame while running (a no-op after the end): moves output into `log`, escalates a cancel,
    // notices the end. RV_ERR_IO: a read or the kill failed, the reason is in `error`; the rest of the
    // frame still runs.
    int pump(rv_editor_log &log, std::string &error);

    // True once pump() has seen the process end and its output drained.
    bool ended() const
    {
        return ended_;
    }
    // True once cancel() began a stop of this run.
    bool cancelled() const
    {
        return cancelled_;
    }
    // Ended, not cancelled, exit code 0 and no signal.
    bool succeeded() const
    {
        return ended_ && !cancelled_ && exit_status().signal == 0 && exit_status().code == 0;
    }
    pid_t pid() const
    {
        return proc_.pid();
    }
    const rv_editor_process::rv_editor_exit &exit_status() const
    {
        return proc_.exit_status();
    }
    // True once the output was cut short of end of file.
    bool output_cut() const
    {
        return proc_.output_cut();
    }

private:
    rv_editor_process proc_;
    std::string name_;
    rv_editor_log_source source_ = rv_editor_log_source::editor;
    uint32_t run_ = 0;
    std::string out_partial_;
    std::string err_partial_;
    std::chrono::steady_clock::time_point cancel_at_{};
    bool cancelled_ = false;
    bool ended_ = false;
    // The SIGKILL failure of this cancel is reported once, not every frame.
    bool kill_failure_reported_ = false;
};

} // namespace rv_editor
