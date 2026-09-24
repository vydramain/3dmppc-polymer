#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "build/rv_editor_build.hpp"
#include "files/rv_editor_files.hpp"
#include "layout/rv_editor_tile.hpp"
#include "log/rv_editor_log.hpp"
#include "nvim/rv_editor_nvim.hpp"
#include "project/rv_editor_project.hpp"
#include "session/rv_editor_session.hpp"

namespace rv_editor
{

// One Output pane's own view of the shared log (LAY-08): which sources it
// shows and whether it follows new lines. The protocol trace is off by default
// (TRM-02).
struct rv_editor_output_view
{
    std::array<bool, 4> show = { true, true, true, false };
    bool follow = true;
};

// One Files pane's dialog: which operation waits for an answer, on what.
struct rv_editor_files_view
{
    enum class rv_editor_files_dialog
    {
        none,
        new_file,
        new_dir,
        rename,
        remove,
    };

    rv_editor_files_dialog dialog = rv_editor_files_dialog::none;
    std::filesystem::path target; // the directory for new entries, the entry otherwise
    char name[256] = {};
    std::string error;
    bool opening = false;
};

// Everything one editor window works with. The models live here, outside the
// tile tree; panes only look at them (docs/adr/0002-tiling.md).
struct rv_editor_app
{
    rv_editor_toolchain tools;
    rv_editor_project project;
    rv_editor_log log;
    rv_editor_build build;
    rv_editor_session session;
    rv_editor_files files;
    std::map<rv_editor_pane_id, rv_editor_output_view> outputs;
    std::map<rv_editor_pane_id, rv_editor_files_view> files_views;
    // Files the user asked to open (Files double click, a new file), for the
    // code editor to take.
    std::vector<std::filesystem::path> open_requests;
    rv_editor_nvim nvim;
    // A code tile had the keyboard this frame: ImGui's own keyboard navigation
    // stays off the next one, so arrows and Tab reach nvim.
    bool text_focus = false;
};

// Looks for the tools and says what it found.
void rv_editor_app_init(rv_editor_app &app);

// Opens a game directory or its disc.toml. A running session and build keep
// running on the project they started with until they end (PRJ-09 holds: one
// session per window).
bool rv_editor_app_open(rv_editor_app &app, const std::filesystem::path &target);

// Why each action cannot run now, or nullptr when it can (UI-04). The text
// lives until the next change to `app`.
const char *rv_editor_app_why_not_build(const rv_editor_app &app);
const char *rv_editor_app_why_not_run(const rv_editor_app &app);
const char *rv_editor_app_why_not_run_last(const rv_editor_app &app);
const char *rv_editor_app_why_not_pause(const rv_editor_app &app);
const char *rv_editor_app_why_not_step(const rv_editor_app &app);
const char *rv_editor_app_why_not_stop(const rv_editor_app &app);

void rv_editor_app_build(rv_editor_app &app);
// Runs the last build, which succeeded; on a paused session this resumes it.
void rv_editor_app_run(rv_editor_app &app);
// Runs the last successful build after a later build failed (BLD-05).
void rv_editor_app_run_last(rv_editor_app &app);
void rv_editor_app_pause(rv_editor_app &app);
void rv_editor_app_step(rv_editor_app &app);
void rv_editor_app_stop(rv_editor_app &app);

// Renames or deletes inside the project; false with the reason.
bool rv_editor_app_rename(rv_editor_app &app, const std::filesystem::path &from, const std::string &name,
    std::string &error);
bool rv_editor_app_remove(rv_editor_app &app, const std::filesystem::path &path, std::string &error);

// Once a frame, before drawing.
void rv_editor_app_update(rv_editor_app &app);

// Before the window closes: cancels the build and ends the runtime, leaving no
// process behind (DEV-09).
void rv_editor_app_shutdown(rv_editor_app &app);

} // namespace rv_editor
