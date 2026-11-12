#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "build/rv_editor_job.hpp"
#include "log/rv_editor_log.hpp"
#include "project/rv_editor_problems.hpp"
#include "project/rv_editor_project.hpp"

namespace rv_editor
{

// The background mppcinspector run: start() once per project open, update() once a frame. The
// report it writes to the cache is read when the run ends and becomes problems(); the frame never waits.
class rv_editor_inspect
{
public:
    // RV_OK, also when nothing runs: no inspector or one with a problem. A new start drops the previous
    // findings. Otherwise the process's RV_ERR_* with the reason in `error`.
    int start(const rv_editor_project &project, const rv_editor_toolchain &tools, rv_editor_log &log, std::string &error);
    // Pumps the process into the log; at its end reads the report. A run that gave none logs why and shows no findings.
    void update(rv_editor_log &log);
    bool running() const
    {
        return running_;
    }
    // Findings of the last run that ended with a report, as source "inspector".
    const std::vector<rv_editor_problem> &problems() const
    {
        return problems_;
    }

private:
    void finish(rv_editor_log &log);

    rv_editor_job job_;
    std::filesystem::path root_;
    std::filesystem::path report_;
    bool running_ = false;
    std::vector<rv_editor_problem> problems_;
};

} // namespace rv_editor
