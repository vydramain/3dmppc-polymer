#pragma once

#include "platform/rv_editor_process.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace rv_editor
{

// A `<tool> --version` check that runs in the background: start() once, poll() once a frame.
// poll() never waits for the tool; a child ended by the deadline or a failed read is reaped by
// a short waitpid in the process destructor. Ends with version() when the tool answers, or with the reason in poll()'s error.
class rv_editor_version_job
{
public:
    rv_editor_version_job() = default;
    rv_editor_version_job(const rv_editor_version_job &) = delete;
    rv_editor_version_job &operator=(const rv_editor_version_job &) = delete;
    rv_editor_version_job(rv_editor_version_job &&) = default;
    rv_editor_version_job &operator=(rv_editor_version_job &&) = default;

    // Starts `tool --version`. RV_OK on success; RV_ERR_ALREADY while a check runs;
    // otherwise the process's RV_ERR_* with the reason in `error`.
    int start(const std::filesystem::path &tool, std::string &error);
    // True while the check runs.
    bool running() const
    {
        return proc_ != nullptr;
    }
    // One step. RV_OK while the check runs, when it ended with a version, and when no check
    // runs; RV_ERR_IO when it ended without a version, the reason in `error`.
    int poll(std::string &error);
    // The first line of the answer; empty until the check ends with one.
    const std::string &version() const
    {
        return version_;
    }

private:
    // Appends the pipe's output. On a failed read the job ends: RV_ERR_IO, reason in `error`.
    int read_output(std::string &error);

    std::unique_ptr<rv_editor_process> proc_;
    std::string out_;
    std::string version_;
    std::chrono::steady_clock::time_point until_{};
};

} // namespace rv_editor
