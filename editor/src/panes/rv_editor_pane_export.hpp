// The Export tile's rules as small pure functions, so the pane only draws what they say.

#pragma once

#include <string>

#include "app/rv_editor_app.hpp"
#include "build/rv_editor_build.hpp"
#include "build/rv_editor_export.hpp"
#include "build/rv_editor_progress.hpp"

namespace rv_editor
{

// Which framed blocks the tile shows for the chosen kind: Kind, Output and Progress always, Web Tools only for a kind
// that needs web tools.
struct rv_editor_export_blocks {
    bool kind = false;
    bool web_tools = false;
    bool output = false;
    bool progress = false;
};

rv_editor_export_blocks rv_editor_export_blocks_shown(rv_editor_export_kind kind);

// How many cards fit one row of `avail` pixels, each at least `card_min` wide with `spacing` between; 1..count.
int rv_editor_export_card_columns(float avail, float card_min, float spacing, int count);

// A card click changes the kind only while no job runs.
bool rv_editor_export_kind_change_allowed(bool busy);

// Why Export is grey: nullptr when it is live; the text of the reason otherwise (a job runs; the kind needs web tools
// that are not installed).
const char *rv_editor_export_button_why(rv_editor_export_kind kind, bool web_tools_installed, bool busy);

// What the Progress block's result line says.
enum class rv_editor_export_result_kind {
    none,      // nothing exported yet
    running,   // a job runs or is being cancelled
    succeeded, // DONE <path>, with the open actions of the kind
    failed,    // FAILED at <step>: <reason>, with Show in Problems
};

struct rv_editor_export_result_view {
    rv_editor_export_result_kind kind = rv_editor_export_result_kind::none;
    std::string path;           // succeeded: the result path
    bool open_folder = false;   // succeeded: Open Folder is offered
    bool open_browser = false;  // succeeded: Open in Browser is offered
    std::string step;           // failed: the label of the step that failed or was interrupted; empty when none did
    std::string reason;         // failed: why
    bool show_problems = false; // failed: Show in Problems is offered
};

// The result line for the job's state, the path it wrote, the reason it failed and its progress; `kind` is the kind
// of the last export (its row in the table gives the open actions).
rv_editor_export_result_view rv_editor_export_result_make(rv_editor_build_state state,
    const std::string &path,
    const std::string &reason,
    const rv_editor_progress &progress,
    rv_editor_export_kind kind);

// Show in Problems: asks the window to bring Problems forward.
void rv_editor_export_show_problems(rv_editor_app &app);

} // namespace rv_editor
