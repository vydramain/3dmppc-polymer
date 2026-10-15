#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <SDL3/SDL.h>
#include "imgui.h"

#include "app/rv_editor_app.hpp"
#include "app/rv_editor_scene_tabs.hpp"
#include "nvim/rv_editor_nvim_grid.hpp"
#include "panes/rv_editor_scene_actions.hpp"
#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// The UI state port a pane gets (app/rv_editor_shell.hpp).
struct rv_editor_ui_port;

// The panes that look at the editor's models. Each draws into the current ImGui
// window and changes the models only through rv_editor_app's commands.

// Shared by more than one pane: dimmed wrapped text, a clock reading, one row
// of a two-column facts table. The variants without theme remain only for pane_session
// and pane_build_result until they pass a theme.
void rv_editor_dim(const std::string &text);
std::string rv_editor_clock(std::chrono::system_clock::time_point at);
void rv_editor_fact(const char *label, const std::string &value, const rv_editor_theme &theme);
void rv_editor_fact(const char *label, const std::string &value);

// Build, Run/Resume, Pause, Step, Stop and the session's state on one row that
// wraps when narrow; Open Project without a project.
void rv_editor_pane_controls(rv_editor_app &app, const rv_editor_theme &theme);

// "No project is open." and an Open Project button, for a pane's empty state.
void rv_editor_open_project_row(rv_editor_app &app, const rv_editor_theme &theme);

// The shared log, through this pane's own filters.
void rv_editor_pane_output(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);
// "Console Output: All", "Console Output: Build #8, errors": what an Output pane's
// filters keep.
std::string rv_editor_output_title(const rv_editor_app &app, rv_editor_pane_id pane);
// "All" (every source but the protocol trace), "All + protocol", or the names shown.
std::string rv_editor_output_sources(const rv_editor_output_view &view, bool capital);
// Source, Level, Find, Follow and Wrap, then Copy, Export and Clear View, or as many
// of those as the row fits; the rest reachable from a "More" menu. Sets copy and
// exporting when the caller should act on the lines it filters out below.
void rv_editor_output_controls(rv_editor_app &app,
    rv_editor_output_view &view,
    bool &copy,
    bool &exporting,
    const rv_editor_theme &theme);

// A code tile: one window of the editor's nvim.
void rv_editor_pane_code(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme, const rv_editor_ui_port &ui);
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
// The New Scene area at the top of the Scene pane while it is asked for (menu_scene.cpp).
void rv_editor_scene_new_area(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui);
// A scene tab for `path` (a PNG or a sound: .png, .wav, .pcm), with a sound's duration read
// now; nullopt for any other file (pane_scene_tabs.cpp).
std::optional<rv_editor_scene_tab> rv_editor_scene_tab_build(const std::filesystem::path &path);
// Draws the Scene tile's tab strip (only when a tab is open) and, when the front
// tab is a picture or a sound, its content. True when the scene viewport belongs
// in front instead (no tabs open, or the scene tab is in front). The front tab
// and Close Tab are queued on `ui` (rv_editor_ui_request).
bool rv_editor_scene_tabs_draw(rv_editor_app &app,
    SDL_Renderer *renderer,
    const rv_editor_theme &theme,
    const rv_editor_ui_port &ui);
// Hierarchy and the scene's Inspector; rv_editor_scene_keys takes Ctrl+Z, Ctrl+Shift+Z,
// Delete and Ctrl+D for the scene while the calling pane has the keyboard.
void rv_editor_pane_hierarchy(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui);
// A mesh object's Pieces node under its row: its model's pieces, picked by click (rv_editor_pane_hierarchy_pieces.cpp).
void rv_editor_hierarchy_pieces(rv_editor_app &app, const rv_editor_scene_object &o, const rv_editor_theme &theme);
// Hierarchy's shelf: Add opens the kinds menu, then the command buttons (rv_editor_pane_hierarchy_actions.cpp).
void rv_editor_hierarchy_shelf(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui);
// What the commands act on, with Clear Picks when pieces are picked (rv_editor_pane_hierarchy_actions.cpp).
void rv_editor_hierarchy_target(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui);
// Runs an action on a copy of the Scene camera, queuing a changed camera (rv_editor_pane_hierarchy_actions.cpp).
void rv_editor_hierarchy_run(rv_editor_app &app, const rv_editor_ui_port &ui, rv_editor_scene_action action);
// The object kinds the Add menus offer, in menu order, and the label each one shows.
// The label points into `kind` (except mesh's "box"): it lives only as long as `kind`.
std::span<const std::string_view> rv_editor_hierarchy_kinds();
const char *rv_editor_scene_kind_label(const std::string &kind);
void rv_editor_pane_scene_inspector(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui);
// The selected object's entity type, Visible and components (rv_editor_pane_scene_components.cpp).
void rv_editor_inspector_components(rv_editor_app &app, const rv_editor_theme &theme, const std::string &id, bool read_only);
void rv_editor_scene_keys(rv_editor_app &app, const rv_editor_ui_port &ui);
// The Scene viewport, and the Scene Toolchest's tools it uses.
void rv_editor_scene_viewport(rv_editor_app &app,
    SDL_Renderer *renderer,
    const rv_editor_theme &theme,
    const rv_editor_ui_port &ui);
void rv_editor_scene_tools(const rv_editor_theme &theme, const rv_editor_ui_port &ui);

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
