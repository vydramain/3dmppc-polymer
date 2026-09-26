#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "build/rv_editor_build.hpp"
#include "release/rv_editor_candidate.hpp"
#include "files/rv_editor_files.hpp"
#include "layout/rv_editor_tile.hpp"
#include "log/rv_editor_log.hpp"
#include "nvim/rv_editor_nvim.hpp"
#include "prefs/rv_editor_prefs.hpp"
#include "project/rv_editor_project.hpp"
#include "session/rv_editor_session.hpp"
#include "terminal/rv_editor_terminal.hpp"

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

// The files a code tile has shown, one tab each, and the one its Close Tab let
// go of until nvim has moved the window on.
struct rv_editor_code_tabs
{
    std::vector<std::string> names;
    std::string dropped;
};

// A Terminal tile's shell, started when the tile is first drawn, and how many
// lines above the screen the tile shows.
struct rv_editor_terminal_view
{
    std::unique_ptr<rv_editor_terminal> term;
    std::string error; // why the shell did not start
    int scroll = 0;
};

// What Observe reads: the table it lists and the paths pinned to watch, each a
// request's keys ("player x"). `read_frame` is the frame it last read on.
struct rv_editor_observe
{
    std::vector<std::string> path;
    std::vector<std::string> pins;
    int64_t read_frame = -1;
};

// The finding being written and the ones saved in this window.
struct rv_editor_findings
{
    char title[160] = {};
    char steps[2048] = {};
    char expected[1024] = {};
    char actual[1024] = {};
    std::filesystem::path capture; // the frame captured for it, if any
    std::vector<std::filesystem::path> saved;
    std::string error;
};

// Release candidates made in this window, oldest first, and the one shown. Kept
// in memory only: the images stay on disk, their checks go with the window.
struct rv_editor_release
{
    std::vector<rv_editor_candidate> candidates;
    size_t selected = 0;
    bool building = false;     // the build job writes a candidate now
    uint32_t building_number = 0;
    uint64_t build_first_seq = 0;
    bool tree_changed_during = false;
    std::string last_failure;  // the newest candidate build did not make one
    int playing = -1;          // the candidate the session runs, or -1
    std::string report;        // the last report written
    std::string error;
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
    std::map<rv_editor_pane_id, rv_editor_code_tabs> code_tabs;
    std::map<rv_editor_pane_id, rv_editor_terminal_view> terminals;
    rv_editor_nvim nvim;
    // A code tile had the keyboard this frame: ImGui's own keyboard navigation
    // stays off the next one, so arrows and Tab reach nvim.
    bool text_focus = false;
    // How the Game tile scales the frame; kept in the view file.
    rv_editor_game_scale game_scale = rv_editor_game_scale::fit;
    // The Game tile has the keyboard; and whether one was drawn this frame: a
    // Game tile closed or behind another tab holds no keys.
    bool game_captured = false;
    bool game_drawn = false;
    // What a Game tile needs to show the frame at 1x, with its rows above it;
    // 0 x 0 before the first frame.
    rv_editor_size game_need{ 0, 0 };
    // A pane's Open Project button was clicked: the window shows its folder dialog.
    bool open_folder_request = false;
    rv_editor_observe observe;
    rv_editor_findings findings;
    rv_editor_release release;
    // The layout in front, for panes that differ by it (Toolchest, Game in Burn).
    rv_editor_layout_preset preset = rv_editor_layout_preset::code;
    // Burn's Candidate & Verify is in front: panes shared with Debug speak of candidates.
    bool release_view = false;
    // Asked by a pane, done by the window next frame: a pane to bring forward, a new
    // Untitled buffer, a Burn mode, a frame capture for Findings.
    rv_editor_pane_kind show_request = rv_editor_pane_kind::empty;
    bool new_file_request = false;
    rv_editor_layout_preset burn_submode_request = rv_editor_layout_preset::code;
    bool capture_request = false;
    // The first log line of the latest build job, for its diagnostics.
    uint64_t build_first_seq = 0;
    // The first log line of the current session, for a finding's log.
    uint64_t session_first_seq = 0;
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
// Why Reload cannot run now; nullptr when it can. Only asked when the session can reload at all.
const char *rv_editor_app_why_not_reload(const rv_editor_app &app);
// The running console can reload its entry script: a directory medium and a Lua entry.
bool rv_editor_app_can_reload(const rv_editor_app &app);

void rv_editor_app_build(rv_editor_app &app);
// Starts a console on `artifact` with memory card `card` (empty: the project's).
bool rv_editor_app_start(rv_editor_app &app, const rv_editor_artifact &artifact, const std::filesystem::path &card = {});

// Release (editor/src/app/rv_editor_app_release.cpp): Build Candidate writes a new
// numbered image; Run Candidate runs the one shown on the development console.
void rv_editor_app_build_candidate(rv_editor_app &app);
const char *rv_editor_app_why_not_run_candidate(const rv_editor_app &app);
void rv_editor_app_run_candidate(rv_editor_app &app);
void rv_editor_app_export_report(rv_editor_app &app);
// Once a frame: a finished candidate build, hashes, the playtest's end.
void rv_editor_app_release_update(rv_editor_app &app, bool build_ended);
// A project file changed: every candidate's sources now differ from the tree.
void rv_editor_app_release_changed(rv_editor_app &app);

// Runs the last build, which succeeded; on a paused session this resumes it.
void rv_editor_app_run(rv_editor_app &app);
// Runs the last successful build after a later build failed (BLD-05).
void rv_editor_app_run_last(rv_editor_app &app);
void rv_editor_app_pause(rv_editor_app &app);
void rv_editor_app_step(rv_editor_app &app);
void rv_editor_app_stop(rv_editor_app &app);
void rv_editor_app_reload(rv_editor_app &app);

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
