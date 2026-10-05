#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include <SDL3/SDL.h>
#include "imgui.h"

#include "app/rv_editor_app.hpp"
#include "nvim/rv_editor_nvim_grid.hpp"
#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// The panes that look at the editor's models. Each draws into the current ImGui
// window and changes the models only through rv_editor_app's commands.

// Shared by more than one pane: dimmed wrapped text, a clock reading, one row
// of a two-column facts table.
void rv_editor_dim(const std::string &text);
std::string rv_editor_clock(std::chrono::system_clock::time_point at);
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
void rv_editor_output_controls(rv_editor_app &app, rv_editor_output_view &view, bool &copy, bool &exporting,
    const rv_editor_theme &theme);

// A code tile: one window of the editor's nvim.
void rv_editor_pane_code(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);

// RGB to ImU32 conversion.
ImU32 rv_editor_rgb(uint32_t rgb);
// One nvim grid cell by cell with cursor.
void rv_editor_nvim_draw_grid(const rv_editor_nvim_screen &screen, const rv_editor_nvim_grid &grid, ImVec2 at,
    ImVec2 cell, int32_t rows, bool cursor);
// Floating windows sorted by zindex, clipped to tile.
void rv_editor_nvim_draw_floats(const rv_editor_nvim_screen &screen, int32_t grid_id, ImVec2 tile_at, ImVec2 cell,
    int32_t tile_cols, int32_t tile_rows, bool tile_focused);

// A shell in the project's directory on a PTY of its own.
void rv_editor_pane_terminal(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);

// The console's own frame, scaled to the tile as View > Game Scale says, and its
// pad while the tile holds the keyboard. Shift+Esc
// lets the keyboard go.
void rv_editor_pane_game(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme);
// Assets: the project's resource files as an Icon Catalog; its PNG pictures live on `renderer`.
void rv_editor_pane_assets(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme);
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
// Saves the Game's frame into the project's findings and attaches it to the one being written.
void rv_editor_findings_capture(rv_editor_app &app);

// Writes the frame the Game tile shows now as a PNG. RV_OK on success, RV_ERR_NOENT
// when no frame of the running session has arrived, RV_ERR_IO when the file cannot be written.
int rv_editor_game_capture(rv_editor_app &app, const std::filesystem::path &path, std::string &error);

// Release: Build Candidate, Run Candidate, Stop, Export Report and the shown
// candidate's state on one row; and the Candidate pane.
void rv_editor_pane_release_controls(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_pane_candidate(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_pane_checks(rv_editor_app &app, const rv_editor_theme &theme);

// The layout's command palette (spec 7): Code, Debug and Burn each their few.
void rv_editor_pane_toolchest(rv_editor_app &app, const rv_editor_theme &theme);

// The last build job's outcome and diagnostics, and the next action.
void rv_editor_pane_build_result(rv_editor_app &app, const rv_editor_theme &theme);

// The latest build's diagnostics with a place, opening it.
void rv_editor_pane_problems(rv_editor_app &app, const rv_editor_theme &theme);

// Find in Project: the query, its scope, and each place, opening it.
void rv_editor_pane_search(rv_editor_app &app, const rv_editor_theme &theme);

// Scene: the open scene document, or how to create or open one; the
// Create/Open row is shared with the other scene panes.
void rv_editor_pane_scene(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme);
void rv_editor_scene_open_row(rv_editor_app &app, const rv_editor_theme &theme);
// The New Scene area at the top of the Scene pane while it is asked for (menu_scene.cpp).
void rv_editor_scene_new_area(rv_editor_app &app, const rv_editor_theme &theme);
// Draws the Scene tile's tab strip (only when a tab is open) and, when the front
// tab is a picture or a sound, its content. True when the scene viewport belongs
// in front instead (no tabs open, or the scene tab is in front).
bool rv_editor_scene_tabs_draw(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme);
// Hierarchy and the scene's Inspector; rv_editor_scene_keys takes Ctrl+Z, Ctrl+Shift+Z,
// Delete and Ctrl+D for the scene while the calling pane has the keyboard.
void rv_editor_pane_hierarchy(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_pane_scene_inspector(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_scene_keys(rv_editor_app &app);
// The Scene viewport, and the Scene Toolchest's tools it uses.
void rv_editor_scene_viewport(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme);
void rv_editor_scene_tools(rv_editor_app &app, const rv_editor_theme &theme);

// The project's run profiles as a list beside a form: Apply, Apply and Restart.
void rv_editor_pane_run_config(rv_editor_app &app, const rv_editor_theme &theme);

// The project tree and the file operations on it.
void rv_editor_pane_files(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);

// The open project, its manifest, and where the tools and the editor's own files are.
void rv_editor_pane_project(rv_editor_app &app, const rv_editor_theme &theme);

// Forward declaration for the empty pane (avoids circular includes).
struct rv_editor_shell;

// Empty pane: displays selectable pane kinds, transforms into the chosen kind.
void rv_editor_pane_empty(rv_editor_shell &shell, rv_editor_pane_id pane, const rv_editor_theme &theme);

} // namespace rv_editor
