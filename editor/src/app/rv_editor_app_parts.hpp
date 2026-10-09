#pragma once
// Parts of rv_editor_app: the views, forms and job records it holds.

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "layout/rv_editor_tile.hpp"
#include "log/rv_editor_log.hpp"
#include "platform/rv_editor_process.hpp"
#include "project/rv_editor_run_profile.hpp"
#include "release/rv_editor_candidate.hpp"
#include "scene/rv_editor_scene.hpp"
#include "terminal/rv_editor_terminal.hpp"
#include "ui/rv_editor_field_sizes.hpp"

namespace rv_editor
{

// Output pane column widths in code-font cells: Time, Level, Source
constexpr float output_column_time_width = 10.0f;
constexpr float output_column_level_width = 5.0f;
constexpr float output_column_source_width = 10.0f;

// One Output pane's own view of the shared log: which sources it
// shows and whether it follows new lines. The protocol trace is off by default.
struct rv_editor_output_view {
    std::array<bool, static_cast<size_t>(rv_editor_log_source::count)> show = { true, true, true, true, false };
    rv_editor_log_level level = rv_editor_log_level::info; // Level: this and worse
    bool follow = true;
    bool wrap = true;
    char search[search_field_size] = {};
    uint64_t hide_before = 0; // Clear View: lines older than this seq are not shown here
    std::string exported;     // where Export wrote, or why it could not
    std::array<float, 3> columns = { output_column_time_width,
        output_column_level_width,
        output_column_source_width }; // in code-font cells
    uint64_t picked_from = 0;         // the selected lines, by seq; 0: none
    uint64_t picked_to = 0;
    float scroll_x = 0.0f; // the lines' horizontal scroll, for the header over them
    int64_t run_pid = 0;   // Run filter: only this pid's lines; 0: all runs
    std::string run_label; // "<kind> #run, pid <pid>", for the Source button and the title
};

// One Files pane's question: which operation waits for an answer, on what.
struct rv_editor_files_view {
    enum class rv_editor_files_dialog {
        none,
        new_file,
        new_dir,
        rename,
        remove,
    };

    rv_editor_files_dialog dialog = rv_editor_files_dialog::none;
    std::filesystem::path target; // the directory for new entries, the entry otherwise
    char name[asset_identifier_field_size] = {};
    std::string error;
    bool opening = false;
    // Delete: what goes, relative to the target's parent, as found when asked.
    std::vector<std::string> doomed;
    size_t doomed_total = 0;
};

// Run Configuration's form: the active profile as edited until Apply.
// filled again whenever the profiles change.
struct rv_editor_run_form {
    uint64_t loaded = 0; // the run_config_revision it was filled at
    char name[short_text_field_size] = {};
    char runtime[filesystem_path_field_size] = {};
    char memcard[filesystem_path_field_size] = {};
    char cwd[filesystem_path_field_size] = {};
    bool mute = false;
    bool paused = false;
    bool fixed_step = false;
    bool reload_on_save = false;
    char args[command_line_field_size] = {};
    char env[command_line_field_size] = {};
};

// The Scene viewport's tool and view: the editor's, never the game's camera.
enum class rv_editor_scene_tool {
    select,
    move,
    rotate,
    scale,
};

// Viewport shading: Textured takes the Filled path but with the scene texture bound.
enum class rv_editor_scene_shading {
    wireframe,
    filled,
    textured,
};

// Viewport initialization: snap step, azimuth and elevation angles, focal distance
constexpr double camera_snap_step_default = 0.25;
constexpr double camera_yaw_default = 30.0;
constexpr double camera_pitch_default = 25.0;
constexpr double camera_distance_default = 8.0;

struct rv_editor_scene_camera {
    rv_editor_scene_tool tool = rv_editor_scene_tool::select;
    bool snap = false;
    double snap_step = camera_snap_step_default;
    bool grid = true;
    rv_editor_scene_shading shading = rv_editor_scene_shading::wireframe;
    double yaw = camera_yaw_default;
    double pitch = camera_pitch_default;
    double distance = camera_distance_default;
    rv_editor_vec3 target{ 0.0, 0.0, 0.0 };
    bool seeking = false; // Seek pressed: the next click picks the point to turn about
};

// The scene panes' own state: Inspector's text for the object shown, the drag
// being made and the values before it, the last refusal to say; the viewport's
// view and its drag of a tool.
struct rv_editor_scene_ui {
    std::string shown;
    char name[identifier_field_size] = {};
    char mesh[asset_identifier_field_size] = {};
    char texture[asset_identifier_field_size] = {};
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
struct rv_editor_assets_ui {
    int folder = 0;
    char filter[short_text_field_size] = {};
    bool details = false;
};

// The files a code tile has shown, one tab each, and the one its Close Tab let
// go of until nvim has moved the window on.
struct rv_editor_code_tabs {
    std::vector<std::string> names;
    std::string dropped;
};

// A Terminal tile's shell, started when the tile is first drawn, and how many
// lines above the screen the tile shows.
struct rv_editor_terminal_view {
    std::unique_ptr<rv_editor_terminal> term;
    std::string error; // why the shell did not start
    int scroll = 0;
    std::filesystem::path cwd;  // where the shell started
    bool focus_request = false; // Window > Terminal: take the keyboard next frame
    std::string paste;          // several lines waiting for Paste or Cancel
};

// What Observe reads: the table it lists and the paths pinned to watch, each a
// request's keys ("player x"). `read_frame` is the frame it last read on.
struct rv_editor_observe {
    std::vector<std::string> path;
    std::vector<std::string> pins;
    int64_t read_frame = -1;
};

// The finding being written and the ones saved in this window.
struct rv_editor_findings {
    char title[title_field_size] = {};
    char steps[test_steps_field_size] = {};
    char expected[report_field_size] = {};
    char actual[report_field_size] = {};
    std::filesystem::path capture; // the frame captured for it, if any
    std::vector<std::filesystem::path> saved;
    std::string error;
    // Test Case: the project's cases, the one chosen (-1: free play), a note, the results given.
    std::vector<std::filesystem::path> cases;
    std::filesystem::file_time_type cases_read{};
    int32_t test_case = -1;
    char note[note_field_size] = {};
    std::vector<std::string> results;
};

// The project's release candidates, oldest first, and the one shown; their records
// are read back when the project opens.
struct rv_editor_release {
    std::vector<rv_editor_candidate> candidates;
    size_t selected = 0;
    bool building = false; // the build job writes a candidate now
    uint32_t building_number = 0;
    uint64_t build_first_seq = 0;
    bool tree_changed_during = false;
    std::string building_revision;   // the sources' version when that build started
    std::string building_burner;     // the tools' versions when that build started,
    std::string building_baker;      // so Apply during a build cannot blank the record
    std::string last_failure;        // the newest candidate build did not make one
    int playing = -1;                // the candidate the session runs, or -1
    uint64_t playtest_first_seq = 0; // the first log line of that playtest
    // The player playing a candidate in its own window: the process, which
    // candidate, since when, whether the operator stopped it, what it printed.
    std::unique_ptr<rv_editor_process> player;
    int player_candidate = -1;
    std::chrono::steady_clock::time_point player_started{};
    bool player_stopped = false;
    std::string player_output;
    std::string player_out_partial;
    std::string player_err_partial;
    std::string report; // the last report written
    std::string error;
};

// A texture bake for Reload/Reload On Save: the burner runs async, one at a
// time; the last outcome (name, ok, message) stays for the UI after it ends.
struct rv_editor_texture_bake {
    std::unique_ptr<rv_editor_process> proc;
    std::string name;          // the texture's disc name
    std::filesystem::path png; // the source baked from
    std::filesystem::path out; // the staged .mppctex the burner writes
    uint32_t build_number = 0; // the running build the staging path is under
    std::string out_partial;
    std::string err_partial;
    std::string err_all; // the burner's stderr, for the failure line
    bool ok = false;
    std::string message; // one line, the last bake's outcome
};

// What waits on the window's unsaved-files question.
enum class rv_editor_unsaved_ask {
    none,
    build,
    run,
    build_restart,
};

// A file the user asked to open, at a line (0: where nvim last had it) and an
// optional 1-based column (0: start of line).
struct rv_editor_open_request {
    std::filesystem::path path;
    int32_t line = 0;
    int32_t column = 0;
};

// Test case results: the pane's choice, and the name each one has in result files.
enum class rv_editor_case_result_kind {
    passed,
    failed,
    blocked,
    count
};

// What a pane asks the app to do; the app's frame runs it. Later kinds add their fields.
enum class rv_editor_pane_request_kind {
    stop,
    force_stop,
    restart,
    cancel_build,
    create_file, // path: the directory, name: the new file
    create_dir,  // path: the directory, name: the new folder
    rename,      // path: the entry, name: its new name
    remove,      // path: the entry
    refresh_files,
    capture_frame,
    save_finding,
    case_new,
    case_result, // path: the case file (empty: exploratory), choice: the result
    add_mark,    // name: the mark's note (empty: none)
    report_problem,
    dismiss_findings_error,
    observe_read,
    observe_open,  // name: the key path, keys separated by spaces ("player x"; empty: the root)
    observe_pin,   // name: the key path to pin, as the pane writes it ("player x")
    observe_unpin, // name: the pinned key path to drop
    observe_refresh,
    profile_select, // choice: the index of the profile to make active
    profile_add,
    profile_delete,        // the active profile
    profile_apply,         // profile: the edited form, for the active profile
    profile_apply_restart, // profile: as profile_apply, then the session restarts
};

struct rv_editor_pane_request {
    rv_editor_pane_request_kind kind = rv_editor_pane_request_kind::stop;
    rv_editor_pane_id pane = rv_editor_tile_none; // the pane that asked; none when the app asks
    std::filesystem::path path = {};              // the entry the request is about
    std::string name = {};                        // the new name the request carries
    // The chosen variant, e.g. the case result; the count of results: none chosen.
    size_t choice = static_cast<size_t>(rv_editor_case_result_kind::count);
    rv_editor_run_profile profile = {}; // the edited profile a profile_apply carries; last: aggregates above stay positional
};

} // namespace rv_editor
