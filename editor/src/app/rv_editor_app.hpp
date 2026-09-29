#pragma once

#include <array>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "app/rv_editor_scene_tabs.hpp"
#include "build/rv_editor_build.hpp"
#include "build/rv_editor_build_map.hpp"
#include "release/rv_editor_candidate.hpp"
#include "files/rv_editor_files.hpp"
#include "layout/rv_editor_tile.hpp"
#include "log/rv_editor_log.hpp"
#include "nvim/rv_editor_nvim.hpp"
#include "platform/rv_editor_process.hpp"
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

// One Output pane's own view of the shared log (LAY-08): which sources it
// shows and whether it follows new lines. The protocol trace is off by default
// (TRM-02).
struct rv_editor_output_view
{
    std::array<bool, static_cast<size_t>(rv_editor_log_source::count)> show = { true, true, true, true, false };
    rv_editor_log_level level = rv_editor_log_level::info; // Level: this and worse
    bool follow = true;
    bool wrap = true;
    char search[128] = {};
    uint64_t hide_before = 0;  // Clear View: lines older than this seq are not shown here
    std::string exported;      // where Export wrote, or why it could not
    std::array<float, 3> columns = { 10.0f, 5.0f, 10.0f }; // Time, Level, Source, in code-font cells
    uint64_t picked_from = 0;  // the selected lines, by seq; 0: none
    uint64_t picked_to = 0;
    float scroll_x = 0.0f;     // the lines' horizontal scroll, for the header over them
    int64_t run_pid = 0;       // Run filter: only this pid's lines; 0: all runs
    std::string run_label;     // "<kind> #run, pid <pid>", for the Source button and the title
};

// One Files pane's question: which operation waits for an answer, on what.
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
    // Delete: what goes, relative to the target's parent, as found when asked.
    std::vector<std::string> doomed;
    size_t doomed_total = 0;
};

// Run Configuration's form: the active profile as edited until Apply (CFG-02),
// filled again whenever the profiles change.
struct rv_editor_run_form
{
    uint64_t loaded = 0; // the run_config_revision it was filled at
    char name[64] = {};
    char runtime[512] = {};
    char memcard[512] = {};
    char cwd[512] = {};
    bool mute = false;
    bool paused = false;
    bool fixed_step = false;
    bool reload_on_save = false;
    char args[1024] = {};
    char env[1024] = {};
};

// The Scene viewport's tool and view (SCN-05, SCL-02): the editor's, never the game's camera.
enum class rv_editor_scene_tool
{
    select,
    move,
    rotate,
    scale,
};

struct rv_editor_scene_camera
{
    rv_editor_scene_tool tool = rv_editor_scene_tool::select;
    bool snap = false;
    double snap_step = 0.25;
    bool grid = true;
    double yaw = 30.0;
    double pitch = 25.0;
    double distance = 8.0;
    rv_editor_vec3 target{ 0.0, 0.0, 0.0 };
    bool seeking = false; // Seek pressed: the next click picks the point to turn about
};

// The scene panes' own state: Inspector's text for the object shown, the drag
// being made and the values before it, the last refusal to say; the viewport's
// view and its drag of a tool.
struct rv_editor_scene_ui
{
    std::string shown;
    char name[128] = {};
    char mesh[256] = {};
    char texture[256] = {};
    std::string editing;
    rv_editor_vec3 before{};
    bool cancelled = false; // Escape took the drag back; it ends when the button is let go
    std::string note;
    rv_editor_scene_camera camera;
    bool gizmo_dragging = false;
    int gizmo_axis = -1;
    std::array<float, 2> gizmo_from{};
    rv_editor_vec3 gizmo_before_position{};
    rv_editor_vec3 gizmo_before_rotation{};
    rv_editor_vec3 gizmo_before_scale{};
};

// Assets' own view: the folder shown (0: all), the filter, icons or details.
struct rv_editor_assets_ui
{
    int folder = 0;
    char filter[64] = {};
    bool details = false;
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
    std::filesystem::path cwd; // where the shell started
    bool focus_request = false; // Window > Terminal: take the keyboard next frame
    std::string paste;          // several lines waiting for Paste or Cancel
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
    // Test Case: the project's cases, the one chosen (-1: free play), a note, the results given.
    std::vector<std::filesystem::path> cases;
    std::filesystem::file_time_type cases_read{};
    int32_t test_case = -1;
    char note[512] = {};
    std::vector<std::string> results;
};

// The project's release candidates, oldest first, and the one shown; their records
// are read back when the project opens (DAT-03).
struct rv_editor_release
{
    std::vector<rv_editor_candidate> candidates;
    size_t selected = 0;
    bool building = false;     // the build job writes a candidate now
    uint32_t building_number = 0;
    uint64_t build_first_seq = 0;
    bool tree_changed_during = false;
    std::string building_revision; // the sources' version when that build started
    std::string last_failure;  // the newest candidate build did not make one
    int playing = -1;          // the candidate the session runs, or -1
    uint64_t playtest_first_seq = 0; // the first log line of that playtest
    // The player playing a candidate in its own window (REL-06): the process, which
    // candidate, since when, whether the operator stopped it, what it printed.
    std::unique_ptr<rv_editor_process> player;
    int player_candidate = -1;
    std::chrono::steady_clock::time_point player_started{};
    bool player_stopped = false;
    std::string player_output;
    std::string player_out_partial;
    std::string player_err_partial;
    std::string report;        // the last report written
    std::string error;
};

// A texture bake for Reload/Reload On Save: the burner runs async, one at a
// time; the last outcome (name, ok, message) stays for the UI after it ends.
struct rv_editor_texture_bake
{
    std::unique_ptr<rv_editor_process> proc;
    std::string name;           // the texture's disc name
    std::filesystem::path png;  // the source baked from
    std::filesystem::path out;  // the staged .mppctex the burner writes
    uint32_t build_number = 0;  // the running build the staging path is under
    std::string out_partial;
    std::string err_partial;
    std::string err_all;        // the burner's stderr, for the failure line
    bool ok = false;
    std::string message;        // one line, the last bake's outcome
};

// What waits on the window's unsaved-files question (BLD-03).
enum class rv_editor_unsaved_ask
{
    none,
    build,
    run,
    build_restart,
};

// A file the user asked to open, at a line (0: where nvim last had it) and an
// optional 1-based column (0: start of line).
struct rv_editor_open_request
{
    std::filesystem::path path;
    int32_t line = 0;
    int32_t column = 0;
};

// Everything one editor window works with. The models live here, outside the
// tile tree; panes only look at them.
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
    // Files the user asked to open (Files, a new file, Problems, Search Results),
    // for the code editor to take, each at its line (0: where nvim last had it).
    std::vector<rv_editor_open_request> open_requests;
    // Binary files among them the user asked to see as text anyway (Files > Open as Text).
    std::set<std::filesystem::path> open_as_text;
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
    bool new_file_request = false;
    // The session in Debug: the profile it started with, and Mark Moment's marks,
    // "frame 812: the door opens late".
    std::string session_profile;
    std::vector<std::string> marks;
    char mark_note[128] = {};
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
    // Run profiles (CFG-01), what stops the active one, and a count of their changes
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
    // The started build's map (rv_editor_build_map_read), read at rv_editor_app_start;
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
void rv_editor_app_scene_open(rv_editor_app &app, const std::filesystem::path &path);
// The next free name ("main", else "sceneN") a new scene would get today.
std::string rv_editor_app_scene_free_name(const rv_editor_app &app);

// Scene tabs (editor/src/panes/rv_editor_pane_scene_tabs.cpp): opens or brings
// forward the Scene tile's tab for a PNG or a sound (.png, .wav, .pcm); does
// nothing for any other file.
void rv_editor_app_scene_tab_open(rv_editor_app &app, const std::filesystem::path &path);
// Creates scenes/<name>.scene.toml (name: non-empty, letters/digits/_/-, must not
// exist yet), puts it on the disc if no pattern already matches it, writes
// src/<id>_scene.hpp when write_cpp and the disc is C++, then opens it. False with
// the reason on any failure; the scene file and whatever else already succeeded stay.
bool rv_editor_app_scene_create(rv_editor_app &app, std::string_view name, bool write_cpp, std::string &error);
bool rv_editor_app_scene_save(rv_editor_app &app, std::string &error);
// The project's first scene, or none; called when a project opens.
void rv_editor_app_scene_first(rv_editor_app &app);

// Writes the profiles to the project, checks the active one again and refills the
// form; a failed write goes to the log.
void rv_editor_app_profiles_save(rv_editor_app &app);

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
// does nothing if a bake is already running.
void rv_editor_app_texture_bake_start(rv_editor_app &app, const std::string &name, const std::filesystem::path &png);
// Once a frame: drains the running bake, reports its outcome, and on success sends
// the baked bytes over the session's reload slot (dropped, not sent, if the session
// ended or another reload is in flight).
void rv_editor_app_texture_bake_update(rv_editor_app &app);

// Build and Run ask first when a named buffer is unsaved (BLD-03); the _saved
// forms go ahead with the files as they are on disk.
void rv_editor_app_build(rv_editor_app &app);
void rv_editor_app_build_saved(rv_editor_app &app);
void rv_editor_app_run_saved(rv_editor_app &app);
// Build and Restart (editor/src/app/rv_editor_app_change.cpp): builds, then restarts
// the live session (or just starts one) once the build has ended.
void rv_editor_app_build_restart(rv_editor_app &app);
void rv_editor_app_build_restart_saved(rv_editor_app &app);
// Called from rv_editor_app_update with whether a build just ended.
void rv_editor_app_build_restart_update(rv_editor_app &app, bool build_ended);
// Run would build first: nothing succeeded yet, the last build did not, or an input changed.
bool rv_editor_app_run_builds(const rv_editor_app &app);
// Buffers with a file name and unsaved changes: what a build would miss.
std::vector<int64_t> rv_editor_app_unsaved(const rv_editor_app &app);
// Starts a console on `artifact` with memory card `card` (empty: the project's).
bool rv_editor_app_start(rv_editor_app &app, const rv_editor_artifact &artifact, const std::filesystem::path &card = {});

// Release (editor/src/app/rv_editor_app_release.cpp): Build Candidate writes a new
// numbered image; Run Candidate runs the one shown on the development console.
void rv_editor_app_build_candidate(rv_editor_app &app);
const char *rv_editor_app_why_not_run_candidate(const rv_editor_app &app);
void rv_editor_app_run_candidate(rv_editor_app &app);
// Player (editor/src/app/rv_editor_app_player.cpp): Run in Player plays the shown
// candidate in the player's own window; Stop Player is the operator's act.
const char *rv_editor_app_why_not_play(const rv_editor_app &app);
void rv_editor_app_play_candidate(rv_editor_app &app);
void rv_editor_app_stop_player(rv_editor_app &app);
void rv_editor_app_player_update(rv_editor_app &app);
void rv_editor_app_export_report(rv_editor_app &app);
// Once a frame: a finished candidate build, hashes, the playtest's end.
void rv_editor_app_release_update(rv_editor_app &app, bool build_ended);
// A project file changed: every candidate's sources now differ from the tree.
void rv_editor_app_release_changed(rv_editor_app &app);

// Runs the latest build, building the saved files first when rv_editor_app_run_builds;
// on a paused session this resumes it.
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

// Logs (editor/src/app/rv_editor_app_logs.cpp): each session and each
// development build keeps its whole log in a file next to it.
void rv_editor_app_attach_session_log(rv_editor_app &app);
void rv_editor_app_attach_build_log(rv_editor_app &app);

} // namespace rv_editor
