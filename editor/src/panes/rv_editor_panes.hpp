#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>
#include "imgui.h"

#include "app/rv_editor_app.hpp"
#include "app/rv_editor_scene_tabs.hpp"
#include "nvim/rv_editor_nvim_grid.hpp"
#include "panes/rv_editor_scene_actions.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

// The UI state port a pane gets (app/rv_editor_shell.hpp).
struct rv_editor_ui_port;

// The panes that look at the editor's models. Each draws into the current ImGui
// window and changes the models only through rv_editor_app's commands.

// Shared by more than one pane: a clock reading (session, observe) and a row of a
// two-column facts table (session, observe, build_result).
// rv_editor_dim: dimmed wrapped text, pane_session only.
void rv_editor_dim(const std::string &text, const rv_editor_theme &theme);
std::string rv_editor_clock(std::chrono::system_clock::time_point at);
void rv_editor_fact(const char *label, const std::string &value, const rv_editor_theme &theme);

// Build, Run/Resume, Pause, Step, Stop and the session's state on one row that
// wraps when narrow; Open Project without a project.
void rv_editor_pane_controls(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui);

// "No project is open." and an Open Project button, for a pane's empty state.
void rv_editor_open_project_row(rv_editor_app &app, const rv_editor_theme &theme);

// The shared log, through this pane's own filters.
void rv_editor_pane_output(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);
// "Console Output: All", "Console Output: Build #8, errors": what an Output pane's
// filters keep.
std::string rv_editor_output_title(const rv_editor_app &app, rv_editor_pane_id pane);
// "All" (every source but the protocol trace), "All + protocol", or the names shown.
std::string rv_editor_output_sources(const rv_editor_output_view &view, bool capital);
// Source, Level, Find, Follow and Wrap, then Copy, Export and Clear View, all in one row. Sets copy and
// exporting when the caller should act on the lines it filters out below.
void rv_editor_output_controls(rv_editor_app &app,
    rv_editor_output_view &view,
    bool &copy,
    bool &exporting,
    const rv_editor_theme &theme);

// The width of that row with Find at its minimum. Call inside a frame.
float rv_editor_output_controls_width();

// The one minimum of the tile of Output, Runtime Log and Build Log, Terminal, Problems and Search Results, whatever
// their state. Call inside a frame.
rv_editor_size rv_editor_output_minimum();
rv_editor_size rv_editor_terminal_minimum();
rv_editor_size rv_editor_problems_minimum();
rv_editor_size rv_editor_search_minimum();
rv_editor_size rv_editor_scene_minimum();
rv_editor_size rv_editor_hierarchy_minimum();
rv_editor_size rv_editor_inspector_minimum();
// The Toolchest's and Release Controls': one minimum per kind, taken from their elements.
rv_editor_size rv_editor_toolchest_minimum();
rv_editor_size rv_editor_release_controls_minimum();

// A code tile: one window of the editor's nvim.
void rv_editor_pane_code(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme, const rv_editor_ui_port &ui);

// What a code tile's status line or a file tab shows for the buffer `name`: the whole path (relative inside `root`), the
// modified marker last, whatever `width` (unused: nothing is cut); `tooltip` is the full path ("" for an Untitled buffer).
struct rv_editor_code_text {
    std::string shown;
    std::string tooltip;
};
rv_editor_code_text rv_editor_code_status_text(const std::string &name, const std::string &root, bool modified, float width);
rv_editor_code_text rv_editor_code_tab_text(const std::string &name, const std::string &root, bool modified, float width);

// The file tabs of a code tile as the tile-tab row: one tab per file (`name` is "" for an Untitled buffer), each tab's
// label whole (relative to `root`, the modified marker last). Returns true when another tab was picked (*active).
struct rv_editor_code_tab_file {
    std::string name;
    bool modified = false;
};
bool rv_editor_code_tab_strip(const std::vector<rv_editor_code_tab_file> &files,
    const std::string &root,
    int *active,
    const rv_editor_theme &theme,
    rv_editor_tab_probe *probe = nullptr);

// The swap-recovery ask of a code tile: in place of its grid while the file has a swap file, nothing otherwise (a
// message and the Recover and Discard buttons, wrapped to the tile's width).
enum class rv_editor_swap_view {
    none,
    recoverable,
    in_use
};
struct rv_editor_swap_action {
    bool recover = false;
    bool discard = false;
};
rv_editor_swap_action rv_editor_code_swap_place(rv_editor_swap_view view, long long pid, const rv_editor_theme &theme);
// This frame's keyboard as nvim keys.
std::string rv_editor_nvim_keys();

// RGB to ImU32 conversion.
ImU32 rv_editor_rgb(uint32_t rgb);
// One nvim grid cell by cell with cursor.
void rv_editor_nvim_draw_grid(const rv_editor_nvim_screen &screen,
    const rv_editor_nvim_grid &grid,
    ImVec2 at,
    ImVec2 cell,
    int32_t rows,
    bool cursor);
// Floating windows sorted by zindex, clipped to tile.
void rv_editor_nvim_draw_floats(const rv_editor_nvim_screen &screen,
    int32_t grid_id,
    ImVec2 tile_at,
    ImVec2 cell,
    int32_t tile_cols,
    int32_t tile_rows,
    bool tile_focused);

// A shell in the project's directory on a PTY of its own.
void rv_editor_pane_terminal(rv_editor_app &app,
    rv_editor_pane_id pane,
    const rv_editor_theme &theme,
    const rv_editor_ui_port &ui);

// The console's own frame, scaled to the tile as View > Game Scale says, and its
// pad while the tile holds the keyboard. Shift+Esc
// lets the keyboard go.
void rv_editor_pane_game(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme, const rv_editor_ui_port &ui);
// Assets: the project's resource files as an Icon Catalog; its PNG pictures live on `renderer`.
void rv_editor_pane_assets(rv_editor_app &app,
    SDL_Renderer *renderer,
    const rv_editor_theme &theme,
    const rv_editor_ui_port &ui_port);
// The code and colour that stand in for a file's icon, by its extension.
void rv_editor_file_chip(const std::filesystem::path &path, const char *&code, uint32_t &color);

// The console's own keys held now, as rv_isource bits
// (src/rv_pconsole/platform/sdl3/rv_pcwindow_sdl3.cpp).
uint64_t rv_editor_game_keys();

// The running session's facts and its persistent Lua state, read-only.
void rv_editor_pane_observe(rv_editor_app &app, const rv_editor_theme &theme);
// What ran, from which build and profile, for how long, and how it ended.
void rv_editor_pane_session(rv_editor_app &app, const rv_editor_theme &theme);

// Capture Frame and Record Finding: local files with the session's facts (T-09, T-10).
void rv_editor_pane_findings(rv_editor_app &app, const rv_editor_theme &theme);
// The project's testcases/*.txt, or free play, and a Passed, Failed or Blocked
// result saved with the session; Failed starts a finding from the case.
void rv_editor_pane_test_case(rv_editor_app &app, const rv_editor_theme &theme);
// Release: Build Candidate, Run Candidate, Stop, Export Report and the shown
// candidate's state on one row; and the Candidate pane.
void rv_editor_pane_release_controls(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_pane_candidate(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_pane_checks(rv_editor_app &app, const rv_editor_theme &theme);
// The candidate's decision line, shown by Release Controls and the Candidate pane (rv_editor_pane_candidate.cpp).
std::string rv_editor_decision_text(const rv_editor_candidate &c);
// The lamp of a check, shown by the Candidate and Release Controls panes.
rv_editor_status_kind rv_editor_check_lamp(const rv_editor_candidate &c, const rv_editor_check &check);

// The layout's command palette (spec 7): Code, Debug and Burn each their few.
void rv_editor_pane_toolchest(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui);

// The last build job's outcome and diagnostics, and the next action.
void rv_editor_pane_build_result(rv_editor_app &app, const rv_editor_theme &theme);

// Export: the open project as a disc, a player or a web page into a chosen directory, and its state.
void rv_editor_pane_export(rv_editor_app &app, const rv_editor_theme &theme);

// The latest build's diagnostics with a place, opening it.
void rv_editor_pane_problems(rv_editor_app &app, const rv_editor_theme &theme);

// Find in Project: the query, its scope, and each place, opening it.
void rv_editor_pane_search(rv_editor_app &app, const rv_editor_theme &theme);

// Scene: the open scene document, or how to create or open one; the
// Create/Open row is shared with the other scene panes.
void rv_editor_pane_scene(rv_editor_app &app,
    SDL_Renderer *renderer,
    const rv_editor_theme &theme,
    const rv_editor_ui_port &ui);
void rv_editor_scene_open_row(rv_editor_app &app, const rv_editor_theme &theme);
// The box of a scene pane with no scene: Open Project when there is no project, else `text` and Create Scene; the least
// width it needs, and the project's scenes as rows that open on a click (rv_editor_pane_scene_none.cpp).
void rv_editor_scene_none_box(rv_editor_app &app,
    const std::string &text,
    rv_editor_label_kind kind,
    const rv_editor_theme &theme,
    float width);
float rv_editor_scene_none_width();
void rv_editor_scene_files_list(rv_editor_app &app, const rv_editor_theme &theme);
// The New Scene question in the Scene pane's message box, the least width its row needs; it draws nothing and returns
// false while the question is closed (menu_scene.cpp).
bool rv_editor_scene_new_box(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui, float width);
float rv_editor_scene_new_width();
bool rv_editor_scene_new_open();
// The message box between the shelf and the viewport: the New Scene question, else an empty box (rv_editor_pane_scene.cpp).
void rv_editor_scene_say(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui);
// The width every row of the Scene pane keeps at least (rv_editor_pane_scene.cpp).
float rv_editor_scene_content_width();
// A scene tab for `path` (a PNG, a sound or a model: .png, .wav, .pcm, .gltf, .glb), with a
// sound's duration read now; nullopt for any other file (pane_scene_tabs.cpp).
std::optional<rv_editor_scene_tab> rv_editor_scene_tab_build(const std::filesystem::path &path);
// Opens `path` in the Scene tile when it has a tab view: queues the tab, moves the layout
// to scene and shows the Scene tile. False, with nothing queued, for any other file.
bool rv_editor_scene_tab_open(rv_editor_app &app, const rv_editor_ui_port &ui, const std::filesystem::path &path);
// Draws the Scene tile's tab strip (only when a tab is open) and, when the front
// tab is a picture, a sound or a model, its content. True when the scene part belongs
// in front instead (the viewport, or the no-pdkkit note on a bare disc): no tabs open,
// or the scene tab is in front. The front tab and Close Tab are queued on `ui` (rv_editor_ui_request).
bool rv_editor_scene_tabs_draw(rv_editor_app &app,
    SDL_Renderer *renderer,
    const rv_editor_theme &theme,
    const rv_editor_ui_port &ui);
// A model tab (.gltf/.glb) of the Scene tile: the model drawn textured through its own camera, which
// the mouse and keys move (rv_editor_scene_tab_model.cpp). Its camera is kept per absolute path.
void rv_editor_scene_tab_model(SDL_Renderer *renderer, const rv_editor_scene_tab &tab, const rv_editor_theme &theme);
// Hierarchy and the scene's Inspector; rv_editor_scene_keys takes Ctrl+Z, Ctrl+Shift+Z,
// Delete and Ctrl+D for the scene while the calling pane has the keyboard.
void rv_editor_pane_hierarchy(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui);
// A mesh object's Pieces node under its row: its model's pieces, picked by click (rv_editor_pane_hierarchy_pieces.cpp).
void rv_editor_hierarchy_pieces(rv_editor_app &app, const rv_editor_scene_object &o, const rv_editor_theme &theme);
// Hierarchy's shelf: Add opens the kinds menu, then the command buttons (rv_editor_pane_hierarchy_actions.cpp).
void rv_editor_hierarchy_shelf(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui);
// What the commands act on, in words, and how to draw them (rv_editor_pane_hierarchy_actions.cpp).
std::string rv_editor_hierarchy_target_text(const rv_editor_app &app, rv_editor_label_kind &kind);
// The width of the shelf's buttons in a row, without the shelf's padding. Call inside a frame.
float rv_editor_hierarchy_shelf_width();
// Runs an action on a copy of the Scene camera, queuing a changed camera (rv_editor_pane_hierarchy_actions.cpp).
void rv_editor_hierarchy_run(rv_editor_app &app, const rv_editor_ui_port &ui, rv_editor_scene_action action);
// The object kinds the Add menus offer, in menu order, and the label each one shows.
// The label points into `kind` (except mesh's "model"): it lives only as long as `kind`.
std::span<const std::string_view> rv_editor_hierarchy_kinds();
const char *rv_editor_scene_kind_label(const std::string &kind);
void rv_editor_pane_scene_inspector(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui);
// Where fields start beside labels `widest` px wide: that plus a gap (at least six font sizes), or 0 when it is more
// than half the row and each field goes on a line below its label instead (rv_editor_pane_scene_inspector.cpp).
float rv_editor_inspector_column(float widest);
// After a label: the field follows on the same line at `column`, or stays on the line below when `column` is 0.
void rv_editor_inspector_field_at(float column);
// The width of the next field: what is left of the row less `reserve`, but never less than `text` shown whole.
void rv_editor_inspector_field_width(const char *text, float reserve);
float rv_editor_inspector_content_width();
// The reason the last edit of the object at `at` was refused, "" when none (rv_editor_pane_scene_fields.cpp).
std::string rv_editor_inspector_refusal(const rv_editor_scene_doc &doc, size_t at);
// The snapshot's description records when it is loaded, else nullptr: the built-ins apply (rv_editor_pane_scene_fields.cpp).
const std::vector<rv_pdkkit::ecs_component_record> *rv_editor_descs_records(const rv_editor_app &app);
// The components of the object at `at`, each a section of its fields by their descriptions (rv_editor_pane_scene_fields.cpp).
void rv_editor_inspector_fields(rv_editor_app &app, const rv_editor_theme &theme, size_t at);
// The selected object's entity type, Visible and Add component (rv_editor_pane_scene_components.cpp).
void rv_editor_inspector_components(rv_editor_app &app, const rv_editor_theme &theme, const std::string &id, bool read_only);
void rv_editor_scene_keys(rv_editor_app &app, const rv_editor_ui_port &ui);
// The Scene viewport, and the Scene Toolchest's tools it uses.
void rv_editor_scene_viewport(rv_editor_app &app,
    SDL_Renderer *renderer,
    const rv_editor_theme &theme,
    const rv_editor_ui_port &ui);
void rv_editor_scene_tools(const rv_editor_theme &theme, const rv_editor_ui_port &ui);
// The widest of its two rows: four small tiles, or Snap, its step and Grid.
float rv_editor_scene_tools_min_width();

// The project's run profiles as a list beside a form: Apply, Apply and Restart.
void rv_editor_pane_run_config(rv_editor_app &app, const rv_editor_theme &theme);

// The project tree and the file operations on it.
void rv_editor_pane_files(rv_editor_app &app,
    rv_editor_pane_id pane,
    const rv_editor_theme &theme,
    const rv_editor_ui_port &ui);

// The open project, its manifest, and where the tools and the editor's own files are.
void rv_editor_pane_project(rv_editor_app &app, const rv_editor_theme &theme);

// Forward declaration for the empty pane (avoids circular includes).
struct rv_editor_shell;

// Empty pane: displays selectable pane kinds, transforms into the chosen kind.
void rv_editor_pane_empty(rv_editor_shell &shell, rv_editor_pane_id pane, const rv_editor_theme &theme);

} // namespace rv_editor
