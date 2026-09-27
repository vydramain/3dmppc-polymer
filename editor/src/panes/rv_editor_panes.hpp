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

// A code tile: one window of the editor's nvim (docs/adr/0005-code-editor-nvim.md).
void rv_editor_pane_code(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);

// A shell in the project's directory on a PTY of its own (TRM-01).
void rv_editor_pane_terminal(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);

// The console's own frame, scaled to the tile as View > Game Scale says, and its
// pad while the tile holds the keyboard (docs/adr/0006-game-frame.md). Shift+Esc
// lets the keyboard go.
void rv_editor_pane_game(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme);

// The console's own keys held now, as rv_isource bits
// (src/rv_pconsole/platform/sdl3/rv_pcwindow_sdl3.cpp).
uint64_t rv_editor_game_keys();

// The running session's facts and its persistent Lua state, read-only (SCN-08).
void rv_editor_pane_observe(rv_editor_app &app, const rv_editor_theme &theme);

// Capture Frame and Record Finding: local files with the session's facts (T-09, T-10).
void rv_editor_pane_findings(rv_editor_app &app, const rv_editor_theme &theme);

// Writes the frame the Game tile shows now as a PNG. False with the reason when
// no frame of the running session has arrived or the file cannot be written.
bool rv_editor_game_capture(rv_editor_app &app, const std::filesystem::path &path, std::string &error);

// Release: Build Candidate, Run Candidate, Stop, Export Report and the shown
// candidate's state on one row; and the Candidate pane.
void rv_editor_pane_release_controls(rv_editor_app &app, const rv_editor_theme &theme);
void rv_editor_pane_candidate(rv_editor_app &app, const rv_editor_theme &theme);

// The layout's command palette (spec 7): Code, Debug and Burn each their few.
void rv_editor_pane_toolchest(rv_editor_app &app, const rv_editor_theme &theme);

// The last build job's outcome and diagnostics, and the next action (BRN-02).
void rv_editor_pane_build_result(rv_editor_app &app, const rv_editor_theme &theme);

// The latest build's diagnostics with a place, opening it (BLD-06).
void rv_editor_pane_problems(rv_editor_app &app, const rv_editor_theme &theme);

// Find in Project: the query, its scope, and each place, opening it (TXT-06).
void rv_editor_pane_search(rv_editor_app &app, const rv_editor_theme &theme);

// The project's run profiles as a form: Apply, Apply and Restart (CFG-01/02).
void rv_editor_pane_run_config(rv_editor_app &app, const rv_editor_theme &theme);

// The project tree and the file operations on it.
void rv_editor_pane_files(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);

// The open project, its manifest, and where the tools and the editor's own files are.
void rv_editor_pane_project(rv_editor_app &app, const rv_editor_theme &theme);

} // namespace rv_editor
