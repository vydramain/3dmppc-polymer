// Build Result: the centre of Burn's Build & Diagnose - what the last build job
// did, its errors and warnings, and what to do next; never an empty Untitled.

#include "panes/rv_editor_panes.hpp"

#include <string>
#include <vector>

#include "imgui.h"

#include "platform/rv_editor_process.hpp"
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

// The process, its exit and whether output was cut, once a build job has ended.
void rv_editor_build_result_summary(rv_editor_app &app, const rv_editor_build &b, const rv_editor_theme &theme)
{
    std::string exit_line = rv_editor_exit_text(b.exit_status());
    if (b.output_cut()) {
        exit_line += " (its output past this point was not kept)";
    }
    if (ImGui::BeginTable("##build_summary", 2, ImGuiTableFlags_SizingStretchProp)) {
        rv_editor_fact("Process", "mppcburner, pid " + std::to_string(b.pid()));
        rv_editor_fact("Exit", exit_line);
        ImGui::EndTable();
    }

    const std::string file = app.log.file_for(b.pid());
    const char *no_log = b.image().empty() ? "This build kept no log file."
                                            : "A candidate image's logs are kept beside the candidate.";
    if (rv_editor_button("Open Build Log", theme, { rv_editor_look::live, file.empty() ? no_log : nullptr })) {
        app.open_requests.push_back({ file, 0 });
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

    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const auto body = [&]() {
        if (b.state() == rv_editor_build_state::idle) {
            ImGui::TextWrapped("Nothing built in this window yet.");
        } else {
            const std::string what = b.image().empty() ? "Development build #" + std::to_string(b.number())
                                                       : "Candidate image " + b.image().filename().string();
            const std::string line = what + ": " + rv_editor_build_state_name(b.state());
            rv_editor_status(line.c_str(), rv_editor_build_lamp(b.state()), theme);
            if (!b.busy()) {
                rv_editor_build_result_summary(app, b, theme);
            }
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
        for (const rv_editor_log_line &l : app.log.lines()) {
            if (l.seq < app.build_first_seq ||
                (l.source != rv_editor_log_source::build && l.source != rv_editor_log_source::candidate) ||
                l.level == rv_editor_log_level::info) {
                continue;
            }
            (l.level == rv_editor_log_level::error ? errors : warnings) += 1;
            found.push_back(&l);
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
        for (const rv_editor_log_line *l : found) {
            rv_editor_log_row("", "build",
                l->level == rv_editor_log_level::error ? rv_editor_severity::error : rv_editor_severity::warning,
                l->text.c_str(), theme);
        }
        rv_editor_log_end(theme);
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
