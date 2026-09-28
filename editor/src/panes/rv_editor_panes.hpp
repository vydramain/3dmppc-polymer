#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <SDL3/SDL.h>

#include "app/rv_editor_app.hpp"
#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// The panes that look at the editor's models. Each draws into the current ImGui
// window and changes the models only through rv_editor_app's commands.

// Build, Run/Resume, Pause, Step, Stop and the session's state on one row that
// wraps when narrow; Open Project without a project.
void rv_editor_pane_controls(rv_editor_app &app, const rv_editor_theme &theme);

// "No project is open." and an Open Project button, for a pane's empty state.
void rv_editor_open_project_row(rv_editor_app &app, const rv_editor_theme &theme);

// The shared log, through this pane's own filters.
void rv_editor_pane_output(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);
// "Output: All", "Output: Build #8, errors": what an Output pane's filters keep.
std::string rv_editor_output_title(const rv_editor_app &app, rv_editor_pane_id pane);

// A code tile: one window of the editor's nvim.
void rv_editor_pane_code(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);

// A shell in the project's directory on a PTY of its own (TRM-01).
void rv_editor_pane_terminal(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);

// The console's own frame, scaled to the tile as View > Game Scale says, and its
// pad while the tile holds the keyboard. Shift+Esc
// lets the keyboard go.
void rv_editor_pane_game(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme);
// Assets: the project's resource files as an Icon Catalog (SCL-03); its PNG pictures live on `renderer`.
void rv_editor_pane_assets(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme);
// The letter and colour that stand in for a file's icon, by its extension.
void rv_editor_file_chip(const std::filesystem::path &path, char &letter, uint32_t &color);

// The console's own keys held now, as rv_isource bits
// (src/rv_pconsole/platform/sdl3/rv_pcwindow_sdl3.cpp).
uint64_t rv_editor_game_keys();

// The running session's facts and its persistent Lua state, read-only (SCN-08).
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

// Writes the frame the Game tile shows now as a PNG. False with the reason when
// no frame of the running session has arrived or the file cannot be written.
bool rv_editor_game_capture(rv_editor_app &app, const std::filesystem::path &path, std::string &error);

// Release: Build Candidate, Run Candidate, Stop, Export Report and the shown
// candidate's state on one row; and the Candidate pane.
void rv_editor_pane_release_controls(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_pane_candidate(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_pane_checks(rv_editor_app &app, const rv_editor_theme &theme);

// The layout's command palette (spec 7): Code, Debug and Burn each their few.
void rv_editor_pane_toolchest(rv_editor_app &app, const rv_editor_theme &theme);

// The last build job's outcome and diagnostics, and the next action (BRN-02).
void rv_editor_pane_build_result(rv_editor_app &app, const rv_editor_theme &theme);

// The latest build's diagnostics with a place, opening it (BLD-06).
void rv_editor_pane_problems(rv_editor_app &app, const rv_editor_theme &theme);

// Find in Project: the query, its scope, and each place, opening it (TXT-06).
void rv_editor_pane_search(rv_editor_app &app, const rv_editor_theme &theme);

// Scene: the open scene document, or how to create or open one (TPL-04); the
// Create/Open row is shared with the other scene panes.
void rv_editor_pane_scene(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_scene_open_row(rv_editor_app &app, const rv_editor_theme &theme);
// Hierarchy and the scene's Inspector; rv_editor_scene_keys takes Ctrl+Z, Ctrl+Shift+Z,
// Delete and Ctrl+D for the scene while the calling pane has the keyboard.
void rv_editor_pane_hierarchy(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_pane_scene_inspector(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_scene_keys(rv_editor_app &app);
// The Scene viewport, and the Scene Toolchest's tools it uses.
void rv_editor_scene_viewport(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_scene_tools(rv_editor_app &app, const rv_editor_theme &theme);

// The project's run profiles as a list beside a form: Apply, Apply and Restart (CFG-01/02).
void rv_editor_pane_run_config(rv_editor_app &app, const rv_editor_theme &theme);
// The same in a window sized for it, opened by app.run_config_open.
void rv_editor_run_config_dialog(rv_editor_app &app, const rv_editor_theme &theme);

// The project tree and the file operations on it.
void rv_editor_pane_files(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);

// The open project, its manifest, and where the tools and the editor's own files are.
void rv_editor_pane_project(rv_editor_app &app, const rv_editor_theme &theme);

} // namespace rv_editor
