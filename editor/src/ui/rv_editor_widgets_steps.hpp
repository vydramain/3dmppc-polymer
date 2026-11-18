// The step strip: one run's steps as cells, in a full form (Export Progress) and a compact text (status bar,
// Runtime Controls).

#pragma once

#include <string>
#include <vector>

#include "build/rv_editor_progress.hpp"
#include "imgui.h"
#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// The mark of a state inside brackets: done "v", running ">", pending " ", failed "x", interrupted "!".
char rv_editor_step_mark(rv_editor_step_state state);

// `[v][v][>][ ][ ] compile`: one `[mark]` per step of the run, then the label of the running step, else of the
// failed or interrupted one; no label when every step is pending or done. Empty for a run with no steps.
std::string rv_editor_steps_compact(const rv_editor_run &run);

// The compact form of the job's latest top-level run; empty when the job has no run.
std::string rv_editor_steps_compact(const rv_editor_progress &progress);

// One cell of the full form: `[v] sources`, and where it lies in the strip (window coordinates of the row).
struct rv_editor_step_cell {
    std::string text;
    ImVec2 min;
    ImVec2 max;
};

struct rv_editor_steps_layout {
    std::vector<rv_editor_step_cell> cells; // one per step of the run, in order
    int rows = 0;                           // the cells wrap to a new row when a cell does not fit `width`
    int permille = 0;                       // the bar: rv_editor_run_permille of the run
};

// Where the cells of `run` go within `width` pixels, measured with the current font. Draws nothing.
rv_editor_steps_layout rv_editor_steps_layout_make(const rv_editor_run &run, float width);

// Draws the full form in the current window: the cells over the available width (`[v] sources > [>] compile`),
// the bar with n/N, the running step's detail. Returns the layout it drew.
rv_editor_steps_layout rv_editor_steps_draw(const rv_editor_run &run, const rv_editor_theme &theme);

} // namespace rv_editor
