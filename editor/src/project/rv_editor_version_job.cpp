// The background `--version` check of an authoring tool: the frame polls it and never waits for the tool.

#include "project/rv_editor_version_job.hpp"

#include "pdk/rv_err.h"

#include <cstddef>
#include <string_view>
#include <utility>

namespace rv_editor
{

namespace
{

// Argument that asks a tool for its version line.
constexpr std::string_view version_arg = "--version";
// How long a tool may take to answer; past it the check is killed and reported.
constexpr int version_timeout_sec = 1;
// Most bytes taken from a pipe per poll: a version line is far shorter.
constexpr size_t version_read_buf_size = 4096;

} // namespace

int rv_editor_version_job::start(const std::filesystem::path &tool, std::string &error)
{
    if (running()) {
        error = "a version check already runs";
        return RV_ERR_ALREADY;
    }
    out_.clear();
    version_.clear();
    auto proc = std::make_unique<rv_editor_process>();
    const int rc = proc->start({ tool.string(), std::string(version_arg) }, {}, error);
    if (rc != RV_OK) {
        return rc;
    }
    proc_ = std::move(proc);
    until_ = std::chrono::steady_clock::now() + std::chrono::seconds(version_timeout_sec);
    return RV_OK;
}

int rv_editor_version_job::read_output(std::string &error)
{
    std::string err;
    std::string read_error;
    if (proc_->read(out_, err, version_read_buf_size, read_error) != RV_OK) {
        proc_.reset();
        error = "version not read: " + read_error;
        return RV_ERR_IO;
    }
    return RV_OK;
}

int rv_editor_version_job::poll(std::string &error)
{
    if (!proc_) {
        return RV_OK;
    }
    if (read_output(error) != RV_OK) {
        return RV_ERR_IO;
    }
    const bool ended = proc_->poll();
    const bool waiting = !ended && std::chrono::steady_clock::now() < until_;
    if (waiting) {
        return RV_OK;
    }
    if (!ended) {
        // stop() gives no reason; reset() kills the child again and reaps it, so the outcome
        // stays the timeout. If that kill also fails, the reap waits for the child to exit.
        proc_->stop(true);
        proc_.reset();
        error = "no answer to " + std::string(version_arg) + " in " + std::to_string(version_timeout_sec) + " s";
        return RV_ERR_IO;
    }
    // The child is gone; what it wrote before it ended may still wait in the pipe.
    if (read_output(error) != RV_OK) {
        return RV_ERR_IO;
    }
    const rv_editor_process::rv_editor_exit exit = proc_->exit_status();
    const size_t line_end = out_.find('\n');
    const std::string first_line = out_.substr(0, line_end);
    const bool understood = exit.exited && exit.signal == 0 && exit.code == 0 && !first_line.empty();
    proc_.reset();
    if (!understood) {
        error = std::string(version_arg) + " is not understood";
        return RV_ERR_IO;
    }
    // No line end within what was read: the line is longer than the buffer, not a short version.
    if (line_end == std::string::npos && out_.size() >= version_read_buf_size) {
        error = "version line too long";
        return RV_ERR_IO;
    }
    version_ = first_line;
    return RV_OK;
}

} // namespace rv_editor
