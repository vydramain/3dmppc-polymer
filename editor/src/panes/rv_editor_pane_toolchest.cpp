// Toolchest: the few commands of the layout in front, one labelled row each
// (spec 7, UX-02) - a palette, not a column of cards.

#include "panes/rv_editor_panes.hpp"

#include <string>

#include "imgui.h"

#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

void rv_editor_toolchest_code(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (rv_editor_command_button("##new", "new-file", "New File", "A new Untitled buffer in the code tile", theme)) {
        app.new_file_request = true;
    }
    if (rv_editor_command_button("##find", "search", "Find in Project", "Search the project's files (Ctrl+Shift+F)",
            theme)) {
        app.project_search.focus = true;
        app.show_request = rv_editor_pane_kind::search;
    }
    if (rv_editor_command_button("##build", "build", "Build", "Build the project (Ctrl+B)", theme,
            { rv_editor_look::live, rv_editor_app_why_not_build(app) })) {
        rv_editor_app_build(app);
    }
}

void rv_editor_toolchest_debug(rv_editor_app &app, const rv_editor_theme &theme)
{
    // Each brings its pane forward or does its one thing; the session itself is below.
    if (rv_editor_command_button("##source", "source", "Source", "The code tile", theme)) {
        app.show_request = rv_editor_pane_kind::code;
    }
    if (rv_editor_command_button("##inspect", "inspector", "Inspect", "The runtime Inspector", theme)) {
        app.show_request = rv_editor_pane_kind::observe;
    }
    const char *why_not_capture = app.session.live() ? nullptr : "No session is running: there is no frame to capture";
    if (rv_editor_command_button("##capture", "capture", "Capture", "Save the Game's frame as a PNG in Findings", theme,
            { rv_editor_look::live, why_not_capture })) {
        app.show_request = rv_editor_pane_kind::findings;
        app.capture_request = true;
    }
    if (rv_editor_command_button("##record", "record", "Record Finding", "Findings: steps, expected, actual", theme)) {
        app.show_request = rv_editor_pane_kind::findings;
    }
    // The session in brief, as its facts say it: nothing invented when there is none.
    ImGui::SeparatorText("Session");
    const rv_editor_session &s = app.session;
    if (s.number() == 0) {
        ImGui::TextWrapped("None yet: Run starts one.");
        return;
    }
    ImGui::Text("Session %u, %s", s.number(), rv_editor_run_state_name(s.state()));
    if (s.live()) {
        ImGui::Text("Frame %lld", static_cast<long long>(s.frame()));
    }
    if (!s.facts().disc.empty()) {
        ImGui::TextWrapped("%s, PDK %s", s.facts().disc.c_str(), s.facts().pdk.c_str());
    }
    if (!s.live() && !s.end_reason().empty()) {
        ImGui::TextWrapped("Ended: %s", s.end_reason().c_str());
    }
}

void rv_editor_toolchest_burn(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (rv_editor_command_button("##candidate", "candidate", "Build Candidate", "A new numbered disc image", theme,
            { rv_editor_look::live, rv_editor_app_why_not_build(app) })) {
        rv_editor_app_build_candidate(app);
    }
    const char *none = app.release.candidates.empty() ? "No candidate yet: Build Candidate first" : nullptr;
    if (rv_editor_command_button("##inspect", "inspect", "Inspect Artifact", "The shown candidate's identity", theme,
            { rv_editor_look::live, none })) {
        app.burn_submode_request = rv_editor_layout_preset::burn;
        app.show_request = rv_editor_pane_kind::candidate;
    }
    if (rv_editor_command_button("##verify", "verify", "Verify", "Read the image's bytes again", theme,
            { rv_editor_look::live, none })) {
        rv_editor_candidate_hash(app.release.candidates[app.release.selected]);
    }
    if (rv_editor_command_button("##report", "report", "Report", "Export the candidate's report", theme,
            { rv_editor_look::live, none })) {
        rv_editor_app_export_report(app);
    }
}

} // namespace

void rv_editor_pane_toolchest(rv_editor_app &app, const rv_editor_theme &theme)
{
    switch (app.preset) {
        case rv_editor_layout_preset::code: rv_editor_toolchest_code(app, theme); return;
        case rv_editor_layout_preset::debug: rv_editor_toolchest_debug(app, theme); return;
        case rv_editor_layout_preset::burn:
        case rv_editor_layout_preset::burn_diagnose: rv_editor_toolchest_burn(app, theme); return;
        default: break;
    }
    // Scene's tools act on a scene document; none is open, so none is offered.
    ImGui::TextWrapped("Select, Move, Rotate and Scale act on a scene document. This project has none open.");
}

} // namespace rv_editor
