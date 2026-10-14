#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "build/rv_editor_build.hpp"
#include "build/rv_editor_job.hpp"
#include "log/rv_editor_log.hpp"
#include "project/rv_editor_project.hpp"

namespace rv_editor
{

// What mppcburner exports a project as: one disc file, a player directory or a web directory.
enum class rv_editor_export_kind : uint8_t {
    disc,
    player,
    web,
};

// The export job: mppcburner in the background, writing one kind of export into the
// destination directory chosen by the user. The job lives outside the UI: the
// frame never waits for the burner.
class rv_editor_export
{
public:
    // RV_OK if the export starts, or an rv_err code with the reason in `error`: busy, no project,
    // no burner or baker, no firmware for a player, no destination directory, no disc id or one that is not a file name for a
    // disc export, no cache directory. The destination is created if it is missing.
    int start(const rv_editor_project &project,
        const rv_editor_toolchain &tools,
        rv_editor_export_kind kind,
        const std::filesystem::path &dest_dir,
        rv_editor_log &log,
        std::string &error);
    // RV_OK if the setup starts, or an rv_err code with the reason in `error`: busy or the burner's problem.
    // Installs the web tools once per user; it needs no project.
    int start_setup(const rv_editor_toolchain &tools, rv_editor_log &log, std::string &error);
    // Once a frame while busy: moves output into the log and notices the end.
    // RV_ERR_IO: a read or the kill failed, the reason is in `error`; the rest of the frame still runs.
    int update(rv_editor_log &log, std::string &error);
    // Stops the burner and everything it started. RV_ERR_ALREADY: no export runs or it is already
    // being cancelled. RV_ERR_IO: SIGTERM was not delivered; the reason is in `error`.
    int cancel(std::string &error);

    rv_editor_build_state state() const
    {
        return state_;
    }
    bool busy() const
    {
        return state_ == rv_editor_build_state::building || state_ == rv_editor_build_state::cancelling;
    }
    // The path the current or last export writes; the finished file or directory once state() is succeeded.
    // Empty after a setup.
    const std::filesystem::path &result() const
    {
        return result_;
    }
    // Why the last run failed: the burner's last stderr line, or the exit text when it printed none. Empty otherwise.
    const std::string &failure_reason() const
    {
        return failure_reason_;
    }

private:
    rv_editor_job job_;
    rv_editor_build_state state_ = rv_editor_build_state::idle;
    std::filesystem::path result_;
    std::string failure_reason_;
    // The started operation's name in the log: "export" or "web setup".
    std::string label_;
    uint32_t runs_ = 0;
};

} // namespace rv_editor
