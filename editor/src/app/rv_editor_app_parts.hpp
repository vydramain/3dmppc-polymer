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
#include "pdklib/rv_math/rv_fx.hpp"
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

// Viewport initialization, raw Q20.12: snap step, azimuth and elevation angles (degrees), focal distance (metres)
constexpr rv_pdklib::fx camera_snap_step_default = rv_pdklib::fx_one / 4; // 0.25
constexpr rv_pdklib::fx camera_yaw_default = 30 * rv_pdklib::fx_one;
constexpr rv_pdklib::fx camera_pitch_default = 25 * rv_pdklib::fx_one;
constexpr rv_pdklib::fx camera_distance_default = 8 * rv_pdklib::fx_one;

struct rv_editor_scene_camera {
    rv_editor_scene_tool tool = rv_editor_scene_tool::select;
    bool snap = false;
    rv_pdklib::fx snap_step = camera_snap_step_default;
    bool grid = true;
    rv_editor_scene_shading shading = rv_editor_scene_shading::wireframe;
    rv_pdklib::fx yaw = camera_yaw_default;
    rv_pdklib::fx pitch = camera_pitch_default;
    rv_pdklib::fx distance = camera_distance_default;
    std::array<rv_pdklib::fx, 3> target{}; // raw Q20.12 x, y, z; the fx_vec3 accessors copy it unchanged
    bool seeking = false; // Seek pressed: the next click picks the point to turn about
    bool operator==(const rv_editor_scene_camera &) const = default;
};

// Q20.12 accessors for the camera fields above: a get copies the raw value (RV_OK), a set stores it. Readouts and ImGui
// coordinates convert through rv_editor_camera_units_of, which is exact: dividing by 4096 only moves the exponent, and a
// raw int32 fits the 53-bit mantissa of a 64-bit float.
inline double rv_editor_camera_units_of(rv_pdklib::fx raw)
{
    return static_cast<double>(raw) / rv_pdklib::fx_one;
}

[[nodiscard]] inline int rv_editor_camera_get_yaw_fx(const rv_editor_scene_camera &c, rv_pdklib::fx &out)
{
    out = c.yaw;
    return RV_OK;
}

inline void rv_editor_camera_set_yaw_fx(rv_editor_scene_camera &c, rv_pdklib::fx value)
{
    c.yaw = value;
}

[[nodiscard]] inline int rv_editor_camera_get_pitch_fx(const rv_editor_scene_camera &c, rv_pdklib::fx &out)
{
    out = c.pitch;
    return RV_OK;
}

inline void rv_editor_camera_set_pitch_fx(rv_editor_scene_camera &c, rv_pdklib::fx value)
{
    c.pitch = value;
}

[[nodiscard]] inline int rv_editor_camera_get_distance_fx(const rv_editor_scene_camera &c, rv_pdklib::fx &out)
{
    out = c.distance;
    return RV_OK;
}

inline void rv_editor_camera_set_distance_fx(rv_editor_scene_camera &c, rv_pdklib::fx value)
{
    c.distance = value;
}

[[nodiscard]] inline int rv_editor_camera_get_snap_step_fx(const rv_editor_scene_camera &c, rv_pdklib::fx &out)
{
    out = c.snap_step;
    return RV_OK;
}

inline void rv_editor_camera_set_snap_step_fx(rv_editor_scene_camera &c, rv_pdklib::fx value)
{
    c.snap_step = value;
}

[[nodiscard]] inline int rv_editor_camera_get_target_fx(const rv_editor_scene_camera &c, rv_pdklib::fx_vec3 &out)
{
    out = rv_pdklib::fx_vec3{ c.target[0], c.target[1], c.target[2] };
    return RV_OK;
}

inline void rv_editor_camera_set_target_fx(rv_editor_scene_camera &c, const rv_pdklib::fx_vec3 &value)
{
    c.target = { value.x, value.y, value.z };
}

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

// A test case listed from testcases/: its file, the text the panel shows, and the file's time at the read.
struct rv_editor_listed_case {
    std::filesystem::path path;
    std::string title;
    std::string steps;
    std::string expected;
    std::filesystem::file_time_type written{};
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
    // Test Case: the listed cases (sorted by path), the one chosen (-1: free play), a note, the results given.
    std::vector<rv_editor_listed_case> cases;
    std::filesystem::file_time_type cases_read{}; // testcases/ time at the last listing
    std::vector<rv_editor_listed_case> skipped;   // unreadable cases: their file time, so a change lists again
    bool cases_failed = false;                    // the last listing could not read the directory's time
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
    texture_bake,          // name: the texture's plan name, path: its png
    scene_move,            // name: the object's id, parent, keep_world
    dismiss_scene_note,    // the move refusal's note
    stop_player,           // the running player's Stop: asked again, it is killed
    cases_list,            // the test cases listed again: testcases/ or a listed case changed
};

struct rv_editor_pane_request {
    rv_editor_pane_request_kind kind = rv_editor_pane_request_kind::stop;
    rv_editor_pane_id pane = rv_editor_tile_none; // the pane that asked; none when the app asks
    std::filesystem::path path = {};              // the entry the request is about
    std::string name = {};                        // the new name the request carries
    // The chosen variant, e.g. the case result; the count of results: none chosen.
    size_t choice = static_cast<size_t>(rv_editor_case_result_kind::count);
    rv_editor_run_profile profile = {}; // the edited profile a profile_apply carries; last: aggregates above stay positional
    std::string parent = {};            // scene_move: the object it goes under (empty: the root)
    bool keep_world = true;             // scene_move: keep where it is in the scene
};

} // namespace rv_editor
