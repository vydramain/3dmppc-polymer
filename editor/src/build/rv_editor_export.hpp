#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "build/rv_editor_build.hpp"
#include "build/rv_editor_job.hpp"
#include "build/rv_editor_setup_progress.hpp"
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

// What the Open button does with a finished export of a kind.
enum class rv_editor_export_open_action : uint8_t {
    folder,
    browser,
};

// One row of the kinds table, one per rv_editor_export_kind in its order: what the Export tile draws and the job uses.
struct rv_editor_export_kind_info {
    rv_editor_export_kind kind;
    // The kind's directory under <project>/export: mppcdisc, bundle, web.
    std::string_view subdir;
    bool needs_web_tools;
    rv_editor_export_open_action open;
    // Text ids (editor/texts/rv_editor_texts.toml) of the kind's name and one-line description.
    std::string_view name_key;
    std::string_view description_key;
    // mppcburner --target value.
    std::string_view target;
    // -o is dest/<disc_id><output_suffix>; empty: -o is the destination directory itself.
    std::string_view output_suffix;
    // The result is dest/<result_name>; empty: the result is the output itself.
    std::string_view result_name;
    // A player is burnt with the firmware beside the burner's real binary (symlinks resolved), the file the
    // burner packs; a missing one is refused before the burner starts.
    bool needs_firmware;
};

// The kinds table.
std::span<const rv_editor_export_kind_info> rv_editor_export_kinds();

// <project_root>/export/<subdir of the kind>.
std::filesystem::path rv_editor_export_default_dir(const std::filesystem::path &project_root, rv_editor_export_kind kind);

// The destination path of each kind as the user edited it; a kind never edited gets its default directory.
// One instance lives until the project closes (reset()).
class rv_editor_export_paths
{
public:
    std::string get(const std::filesystem::path &project_root, rv_editor_export_kind kind) const;
    void set(rv_editor_export_kind kind, const std::string &path);
    void reset();

private:
    std::array<std::string, 3> edited_;
};

// A file an export writes into its destination after it succeeded: its presence says the directory was made by an export.
inline constexpr std::string_view rv_editor_export_mark_name = ".mppcexport";

// Makes `dir` ready for a new export: a missing directory is created, an empty one or one holding the mark is
// emptied (the mark goes too). RV_OK, or an rv_err code with the reason in `error` when the directory holds
// anything without the mark (nothing is deleted then) or cannot be created or emptied.
int rv_editor_export_prepare_dir(const std::filesystem::path &dir, std::string &error);

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
    // True while the started job is the web setup.
    bool setup_job() const
    {
        return setup_job_;
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
    // The web setup's ladder position from its last stderr line; step 0 (default) for an export or before any ladder line.
    const rv_editor_setup_progress &setup_progress() const
    {
        return setup_progress_;
    }

private:
    rv_editor_job job_;
    rv_editor_build_state state_ = rv_editor_build_state::idle;
    std::filesystem::path result_;
    std::string failure_reason_;
    // The started operation's name in the log: "export" or "web setup".
    std::string label_;
    // True while the started job is the web setup; only its stderr is read as the ladder.
    bool setup_job_ = false;
    rv_editor_setup_progress setup_progress_;
    // The setup's stderr after its last line end: a line cut by the frame waits here for its rest.
    std::string setup_tail_;
    uint32_t runs_ = 0;
};

// True while the running job is the web setup, from its first frame: the bar shows before any ladder line came.
bool rv_editor_export_setup_bar_due(const rv_editor_export &exporter);

} // namespace rv_editor
