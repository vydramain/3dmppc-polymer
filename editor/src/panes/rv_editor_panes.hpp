#pragma once

#include "app/rv_editor_app.hpp"
#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// The panes that look at the editor's models. Each draws into the current ImGui
// window and changes the models only through rv_editor_app's commands.

// Build, Run/Resume, Pause, Step, Stop; the session and the build job's state.
void rv_editor_pane_controls(rv_editor_app &app, const rv_editor_theme &theme);

// The shared log, through this pane's own filters.
void rv_editor_pane_output(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);

// The project tree and the file operations on it.
void rv_editor_pane_files(rv_editor_app &app, rv_editor_pane_id pane, const rv_editor_theme &theme);

// The open project, its manifest, and where the tools and the editor's own files are.
void rv_editor_pane_project(rv_editor_app &app, const rv_editor_theme &theme);

} // namespace rv_editor
