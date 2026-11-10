#pragma once

#include <string>

namespace rv_editor
{

// The part of the current ladder step that a progress line has reported.
enum class rv_editor_setup_part {
    none,         // no progress line in this step yet
    share,        // "<file>: <pct>%" or "<file>: <pct>% of <size>"; the percent is in rv_editor_setup_progress::percent
    size_unknown, // "<file>: size unknown"
};

// Where `mppcburner setup` stands, read from its "[n/N] label  detail" lines.
struct rv_editor_setup_progress {
    int step = 0; // 0 until the first ladder line
    int total = 0;
    std::string label; // the step's label as printed, e.g. "install"
    rv_editor_setup_part part = rv_editor_setup_part::none;
    int percent = 0;  // 0-100, valid for part == share
    std::string file; // the file the share or size_unknown line named; empty when it named none
};

// True when the line is a ladder line, which updates `progress`. Any other line (log text, a malformed
// prefix, a malformed progress tail) leaves `progress` unchanged; that is not an error.
bool rv_editor_setup_feed(const std::string &line, rv_editor_setup_progress &progress);

// Overall share of the setup in permille: completed steps plus the in-step part, over the total steps.
// 0 before the first ladder line, and for a state outside 1 <= step <= total.
int rv_editor_setup_permille(const rv_editor_setup_progress &progress);

} // namespace rv_editor
