// Build Result: the centre of Burn's Build & Diagnose - what the last build job
// did, its errors and warnings, and what to do next; never an empty Untitled.

#include "panes/rv_editor_panes.hpp"

#include <string>
#include <vector>

#include "imgui.h"

#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

rv_editor_status_kind rv_editor_build_lamp(rv_editor_build_state state)
{
    switch (state) {
        case rv_editor_build_state::succeeded: return rv_editor_status_kind::ok;
        case rv_editor_build_state::failed: return rv_editor_status_kind::error;
        case rv_editor_build_state::building:
        case rv_editor_build_state::cancelling: return rv_editor_status_kind::busy;
        case rv_editor_build_state::cancelled: return rv_editor_status_kind::warning;
        default: return rv_editor_status_kind::idle;
    }
}

} // namespace

void rv_editor_pane_build_result(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    const rv_editor_build &b = app.build;
    if (b.state() == rv_editor_build_state::idle) {
        ImGui::TextWrapped("Nothing built in this window yet.");
    } else {
        const std::string what = b.image().empty() ? "Development build #" + std::to_string(b.number())
                                                   : "Candidate image " + b.image().filename().string();
        const std::string line = what + ": " + rv_editor_build_state_name(b.state());
        rv_editor_status(line.c_str(), rv_editor_build_lamp(b.state()), theme);
    }

    if (rv_editor_button("Build", theme, { rv_editor_look::live, rv_editor_app_why_not_build(app) })) {
        rv_editor_app_build(app);
    }
    ImGui::SameLine();
    if (rv_editor_button("Build Candidate", theme, { rv_editor_look::live, rv_editor_app_why_not_build(app) })) {
        rv_editor_app_build_candidate(app);
    }
    ImGui::SameLine();
    const rv_editor_state cancel = b.state() == rv_editor_build_state::building
        ? rv_editor_state{}
        : rv_editor_state{ rv_editor_look::live, "No build is running" };
    if (rv_editor_button("Cancel Build", theme, cancel)) {
        app.build.cancel();
    }

    // The job's own error and warning lines, newest job only; all of them stay in the Build Log.
    std::vector<const rv_editor_log_line *> found;
    size_t errors = 0;
    size_t warnings = 0;
    for (const rv_editor_log_line &line : app.log.lines()) {
        if (line.seq < app.build_first_seq || line.source != rv_editor_log_source::build ||
            line.level == rv_editor_log_level::info) {
            continue;
        }
        (line.level == rv_editor_log_level::error ? errors : warnings) += 1;
        found.push_back(&line);
    }
    if (b.state() == rv_editor_build_state::idle) {
        return;
    }
    ImGui::SeparatorText("Diagnostics of this build");
    ImGui::Text("%zu errors, %zu warnings", errors, warnings);
    if (found.empty()) {
        ImGui::TextWrapped(b.busy() ? "None so far." : "None. The full output is in the Build Log.");
        return;
    }
    rv_editor_log_begin("##diag", ImVec2(0, 0), theme);
    for (const rv_editor_log_line *line : found) {
        rv_editor_log_row("", "build",
            line->level == rv_editor_log_level::error ? rv_editor_severity::error : rv_editor_severity::warning,
            line->text.c_str(), theme);
    }
    rv_editor_log_end(theme);
}

} // namespace rv_editor
