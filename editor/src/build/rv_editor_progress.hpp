#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rv_editor
{

enum class rv_editor_step_state {
    pending,
    running,
    done,
    failed,
    interrupted, // the job ended with no [end] while the step ran
};

enum class rv_editor_run_end {
    open,        // no [end] yet
    ok,          // [end] ok
    failed,      // [end] failed: reason
    interrupted, // the job ended with no [end]
};

struct rv_editor_run;

struct rv_editor_step {
    std::string label;  // from the [plan]; from the first [n/N] line that names it when there is no plan
    std::string detail; // the latest detail of this step, e.g. "emsdk.tar.xz 38%"
    rv_editor_step_state state = rv_editor_step_state::pending;
    std::vector<rv_editor_run> runs; // sub-runs of other tools that started while this step ran
};

// One tool's run: its steps (as many as the plan, or N of the first [n/N]) and how it ended.
struct rv_editor_run {
    std::string tool;
    std::vector<rv_editor_step> steps;
    rv_editor_run_end end = rv_editor_run_end::open;
    std::string reason; // after "[end] failed: "
};

// The tree of one job's output: the top-level runs in the order they started.
struct rv_editor_progress {
    std::vector<rv_editor_run> runs;
};

// Reads one line (a trailing CR is dropped). True for a protocol line, which updates the tree; foreign lines are ignored.
bool rv_editor_progress_feed(rv_editor_progress &progress, std::string_view line);

// Reads every line of a chunk, '\n' or '\r' ending a line (a tool redraws a progress line with '\r').
// True when any line was a protocol line.
bool rv_editor_progress_feed_text(rv_editor_progress &progress, std::string_view chunk);

// The job has exited: every open run ends interrupted and its running step becomes interrupted. Closed runs stay as they are.
void rv_editor_progress_finish(rv_editor_progress &progress);

// The percent 0-100 a step detail ends with ("<file> 38%", "<file>: 38% of <size>"); -1 when it has none.
int rv_editor_step_percent(const rv_editor_step &step);

// Overall share of a run in permille: finished steps plus the running step's percent, over the steps; 1000 for a run that
// ended ok; 0 for a run with no steps.
int rv_editor_run_permille(const rv_editor_run &run);

} // namespace rv_editor
