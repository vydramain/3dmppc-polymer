#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "app/rv_editor_app_parts.hpp"
#include "ui/rv_editor_field_sizes.hpp"
#include "app/rv_editor_scene_tabs.hpp"
#include "build/rv_editor_build.hpp"
#include "build/rv_editor_build_map.hpp"
#include "files/rv_editor_files.hpp"
#include "layout/rv_editor_tile.hpp"
#include "log/rv_editor_log.hpp"
#include "nvim/rv_editor_nvim.hpp"
#include "prefs/rv_editor_prefs.hpp"
#include "project/rv_editor_change.hpp"
#include "project/rv_editor_problems.hpp"
#include "project/rv_editor_project.hpp"
#include "project/rv_editor_run_profile.hpp"
#include "project/rv_editor_search.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "session/rv_editor_session.hpp"
#include "terminal/rv_editor_terminal.hpp"

namespace rv_editor
{

// Everything one editor window works with. The models live here, outside the
// tile tree; panes only look at them.
struct rv_editor_app {
    rv_editor_toolchain tools;
    rv_editor_project project;
    rv_editor_log log;
    rv_editor_build build;
    rv_editor_session session;
    rv_editor_files files;
    std::map<rv_editor_pane_id, rv_editor_output_view> outputs;
    std::map<rv_editor_pane_id, rv_editor_files_view> files_views;
    // Files the user asked to open (Files, a new file, Problems, Search Results),
    // for the code editor to take, each at its line (0: where nvim last had it).
    std::vector<rv_editor_open_request> open_requests;
    // Binary files among them the user asked to see as text anyway (Files > Open as Text).
    std::set<std::filesystem::path> open_as_text;
    std::map<rv_editor_pane_id, rv_editor_code_tabs> code_tabs;
    std::map<rv_editor_pane_id, rv_editor_terminal_view> terminals;
    // Shells of closed tiles, hung up and stepped each frame until they have ended.
    std::vector<std::unique_ptr<rv_editor_pty>> stopping_ptys;
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
    // The Game tile's own minimum: its mode row and status line (the picture scales
    // below 1x to fit); 0 x 0 before the first frame.
    rv_editor_size game_need{ 0, 0 };
    // The picture area the Game tile gives a running game, measured at every draw.
    rv_editor_size game_area{ 0, 0 };
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
    // Untitled buffer.
    rv_editor_pane_kind show_request = rv_editor_pane_kind::empty;
    // A double click in Assets asked for this layout; nullopt: none (see rv_editor_shell_take_layout_request).
    std::optional<rv_editor_layout_preset> layout_request;
    bool new_file_request = false;
    // The session in Debug: the profile it started with, and Mark Moment's marks,
    // "frame 812: the door opens late".
    std::string session_profile;
    std::vector<std::string> marks;
    char mark_note[mark_note_field_size] = {};
    // The first log line of the latest build job, for its diagnostics.
    uint64_t build_first_seq = 0;
    // What that job's lines name, re-read when the log changes; and when the job ended.
    std::vector<rv_editor_problem> problems;
    uint64_t problems_revision = 0;
    std::filesystem::file_time_type build_ended{};
    rv_editor_search_view project_search;
    // A saved project file changed since the latest development build started, so
    // Run builds first; unknown at start, so true. Run started that build: it runs next.
    bool inputs_changed = true;
    bool run_after_build = false;
    // Build or Run met unsaved named buffers: the window asks before either goes ahead.
    rv_editor_unsaved_ask unsaved_ask = rv_editor_unsaved_ask::none;
    // The first log line of the current session, for a finding's log.
    uint64_t session_first_seq = 0;
    // Run profiles, what stops the active one, and a count of their changes
    // for the form; Apply and Restart waits for the session to end, then runs.
    rv_editor_run_config run_config;
    std::string run_problem;
    uint64_t run_config_revision = 1;
    rv_editor_run_form run_form;
    bool run_after_stop = false;
    // Build and Restart: a build runs for it; once it ends, the live session (if any)
    // stops, then the new build starts. restart_after_stop is the wait for that stop.
    bool restart_after_build = false;
    bool restart_after_stop = false;
    // The scene the Scene layout edits, if one is open, and why the last open failed.
    std::unique_ptr<rv_editor_scene_doc> scene;
    std::string scene_error;
    rv_editor_scene_ui scene_ui;
    rv_editor_assets_ui assets_ui;
    rv_editor_scene_tabs scene_tabs;
    // The started build's map (rv_editor_build_map_read), read at rv_editor_app_run_session;
    // empty once the session is not live.
    std::map<std::string, rv_editor_map_entry> build_map;
    // The file the last-used Code tile shows; the window sets this.
    std::filesystem::path code_file;
    // Reload On Save: files queued to reload, oldest first, no duplicates; reload_due
    // is when the queue may start draining, zero: none due.
    std::vector<std::filesystem::path> reload_queue;
    std::chrono::steady_clock::time_point reload_due{};
    // Reload on a texture: the running bake, if any, and the last one's outcome.
    rv_editor_texture_bake texture_bake;
};

// Scene (editor/src/app/rv_editor_app_scene.cpp): the project's scenes/*.scene.toml,
// the open one, a new one under a free name, and its save.
std::vector<std::filesystem::path> rv_editor_app_scene_files(const rv_editor_app &app);
bool rv_editor_app_scene_dirty(const rv_editor_app &app);
std::string rv_editor_app_scene_name(const rv_editor_app &app);
// If the open scene is unsaved: does nothing when path is that same scene, else
// leaves it open and sets scene_error naming both, refusing to open path.
// RV_OK when opened or already open; RV_ERR_BUSY when an unsaved scene refuses; the load's RV_ERR_* otherwise.
int rv_editor_app_scene_open(rv_editor_app &app, const std::filesystem::path &path);
// The next free name ("main", else "sceneN") a new scene would get today.
std::string rv_editor_app_scene_free_name(const rv_editor_app &app);

// Scene tabs (editor/src/panes/rv_editor_pane_scene_tabs.cpp): opens or brings
// forward the Scene tile's tab for a PNG or a sound (.png, .wav, .pcm); does
// nothing for any other file.
void rv_editor_app_scene_tab_open(rv_editor_app &app, const std::filesystem::path &path);
// Creates scenes/<name>.scene.toml (name: non-empty, letters/digits/_/-, must not
// exist yet), puts it on the disc if no pattern already matches it, writes
// src/<id>_scene.hpp when write_cpp and the disc is C++, then opens it. RV_OK on success,
// RV_ERR_INVAL if name is invalid, RV_ERR_* from scene_save, code from manifest_add_pattern if
// disc.toml update fails, or from codegen_write if C++ write fails; the scene file and whatever else
// already succeeded stay. Reason in error.
int rv_editor_app_scene_create(rv_editor_app &app, std::string_view name, bool write_cpp, std::string &error);
// Saves the scene if dirty. RV_OK when already saved or successfully saved; RV_ERR_* on save failure.
int rv_editor_app_scene_save(rv_editor_app &app, std::string &error);
// The project's first scene, or none; called when a project opens.
// RV_OK when there is none; otherwise rv_editor_app_scene_open's code.
int rv_editor_app_scene_first(rv_editor_app &app);

// Writes the profiles to the project, checks the active one again and refills the
// form. Returns RV_OK on success or RV_ERR_* on save failure.
int rv_editor_app_profiles_save(rv_editor_app &app);

// Looks for the tools and says what it found.
void rv_editor_app_init(rv_editor_app &app);

// Opens a game directory or its disc.toml. A running session and build keep
// running on the project they started with until they end. RV_OK on success, RV_ERR_* on failure.
int rv_editor_app_open(rv_editor_app &app, const std::filesystem::path &target);

// Why each action cannot run now, or nullptr when it can. The text
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
// The change classifier (project/rv_editor_change.hpp) against this app's project,
// manifest and running build's map (editor/src/app/rv_editor_app_change.cpp).
rv_editor_change_plan rv_editor_app_change_for(const rv_editor_app &app, const std::filesystem::path &file);
// Reload On Save: `changed` joins the queue when its plan is reload_entry/reload_module.
void rv_editor_app_reload_queue_note(rv_editor_app &app, const std::filesystem::path &changed);
// Once a frame: sends the next queued reload once it has settled and the last answered.
void rv_editor_app_reload_queue_update(rv_editor_app &app);

// Texture bake (editor/src/app/rv_editor_app_texture.cpp): a refresh_texture reload
// bakes the changed PNG with the burner before sending it, one bake at a time,
// async. True while one runs; when `name` is not null it receives what it bakes.
bool rv_editor_app_texture_bake_busy(const rv_editor_app &app, std::string *name = nullptr);
// Starts baking `name` (the texture's disc name) from `png` into staging; logs and
// does nothing if a bake is already running. RV_OK on success, RV_ERR_BUSY if a bake is running, RV_ERR_IO on filesystem
// failure, or the start error code.
int rv_editor_app_texture_bake_start(rv_editor_app &app, const std::string &name, const std::filesystem::path &png);
// Once a frame: drains the running bake, reports its outcome, and on success sends
// the baked bytes over the session's reload slot (dropped, not sent, if the session
// ended or another reload is in flight).
void rv_editor_app_texture_bake_update(rv_editor_app &app);

// Build and Run ask first when a named buffer is unsaved; the _saved
// forms go ahead with the files as they are on disk.
// RV_OK: build started. RV_ERR_PENDING: waits on the unsaved-files answer (unsaved_ask set), nothing started.
// RV_ERR_BUSY: refused; other RV_ERR_*: failed, reason in the log.
int rv_editor_app_build(rv_editor_app &app);
int rv_editor_app_build_saved(rv_editor_app &app);
int rv_editor_app_run_saved(rv_editor_app &app);
// Build and Restart (editor/src/app/rv_editor_app_change.cpp): builds, then restarts
// the live session (or just starts one) once the build has ended.
// RV_OK: build started. RV_ERR_PENDING: waits on the unsaved-files answer (unsaved_ask set), nothing started.
// RV_ERR_BUSY: refused; other RV_ERR_*: failed, reason in the log.
int rv_editor_app_build_restart(rv_editor_app &app);
int rv_editor_app_build_restart_saved(rv_editor_app &app);
// Called from rv_editor_app_update with whether a build just ended.
void rv_editor_app_build_restart_update(rv_editor_app &app, bool build_ended);
// Run would build first: nothing succeeded yet, the last build did not, or an input changed.
bool rv_editor_app_run_builds(const rv_editor_app &app);
// Buffers with a file name and unsaved changes: what a build would miss.
std::vector<int64_t> rv_editor_app_unsaved(const rv_editor_app &app);
// Runs a session on `artifact` with memory card `card` (empty: the project's): state directory,
// launch, runtime start, build map, session log. Returns RV_OK on success or RV_ERR_* on failure.
int rv_editor_app_run_session(rv_editor_app &app, const rv_editor_artifact &artifact, const std::filesystem::path &card = {});

// Release (editor/src/app/rv_editor_app_release.cpp): Build Candidate writes a new
// numbered image; Run Candidate runs the one shown on the development console.
// Returns RV_OK on success or RV_ERR_* on failure.
int rv_editor_app_build_candidate(rv_editor_app &app);
const char *rv_editor_app_why_not_run_candidate(const rv_editor_app &app);
// Returns RV_OK on success or RV_ERR_* on failure.
int rv_editor_app_run_candidate(rv_editor_app &app);
// Player (editor/src/app/rv_editor_app_player.cpp): Run in Player plays the shown
// candidate in the player's own window; Stop Player is the operator's act.
const char *rv_editor_app_why_not_play(const rv_editor_app &app);
// Returns RV_OK on success or RV_ERR_* on failure.
int rv_editor_app_play_candidate(rv_editor_app &app);
void rv_editor_app_stop_player(rv_editor_app &app);
bool rv_editor_app_player_running(const rv_editor_app &app);
void rv_editor_app_player_update(rv_editor_app &app);
// Returns RV_OK on success or RV_ERR_* on failure.
int rv_editor_app_export_report(rv_editor_app &app);
// Once a frame: a finished candidate build, hashes, the playtest's end.
void rv_editor_app_release_update(rv_editor_app &app, bool build_ended);
// A project file changed: every candidate's sources now differ from the tree.
void rv_editor_app_release_changed(rv_editor_app &app);

// Runs the latest build, building the saved files first when rv_editor_app_run_builds; on a paused session this resumes it.
// RV_OK: run or build started (a rebuild comes first), or pause lifted. RV_ERR_PENDING: waits on the unsaved-files answer
// (unsaved_ask set), nothing started. RV_ERR_BUSY: refused; other RV_ERR_*: failed, reason in the log.
int rv_editor_app_run(rv_editor_app &app);
// Runs the last successful build after a later build failed. Returns RV_OK or RV_ERR_*.
int rv_editor_app_run_last(rv_editor_app &app);
// RV_OK or the session's RV_ERR_* code.
int rv_editor_app_pause(rv_editor_app &app);
int rv_editor_app_step(rv_editor_app &app);
int rv_editor_app_stop(rv_editor_app &app);
// Force Stop: SIGKILL to the runtime; logs the outcome (warning when there is nothing to stop).
int rv_editor_app_force_stop(rv_editor_app &app);
void rv_editor_app_reload(rv_editor_app &app);

// Renames or deletes inside the project; RV_OK or RV_ERR_* with the reason in error.
int rv_editor_app_rename(rv_editor_app &app, const std::filesystem::path &from, const std::string &name, std::string &error);
int rv_editor_app_remove(rv_editor_app &app, const std::filesystem::path &path, std::string &error);

// Once a frame, before drawing.
void rv_editor_app_update(rv_editor_app &app);

// Before the window closes: cancels the build and ends the runtime, leaving no
// process behind.
void rv_editor_app_shutdown(rv_editor_app &app);

// Logs (editor/src/app/rv_editor_app_logs.cpp): each session and each
// development build keeps its whole log in a file next to it.
void rv_editor_app_attach_session_log(rv_editor_app &app);
void rv_editor_app_attach_build_log(rv_editor_app &app);

} // namespace rv_editor
