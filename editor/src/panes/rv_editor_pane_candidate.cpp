// Release: the controls strip and the Candidate pane - which disc image is
// being checked, what was checked on its bytes, and the decision.

#include "panes/rv_editor_panes.hpp"

#include <string>
#include <vector>

#include "imgui.h"

#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

void rv_editor_dim_text(const std::string &text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void rv_editor_row(const char *label, const std::string &value)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", value.c_str());
}

rv_editor_status_kind rv_editor_check_lamp(const rv_editor_candidate &c, const rv_editor_check &check)
{
    if (check.state != rv_editor_check_state::not_run && !rv_editor_check_valid(c, check)) {
        return rv_editor_status_kind::error;
    }
    switch (check.state) {
        case rv_editor_check_state::passed: return rv_editor_status_kind::ok;
        case rv_editor_check_state::running: return rv_editor_status_kind::busy;
        case rv_editor_check_state::failed:
        case rv_editor_check_state::blocked: return rv_editor_status_kind::error;
        case rv_editor_check_state::skipped: return rv_editor_status_kind::warning;
        default: return rv_editor_status_kind::idle;
    }
}

std::string rv_editor_decision_text(const rv_editor_candidate &c)
{
    if (c.bytes_changed && c.decision != rv_editor_decision::none) {
        return "Decision void: it was made on bytes the image no longer holds";
    }
    switch (c.decision) {
        case rv_editor_decision::approved: return "Approved for release at " + c.decided_at;
        case rv_editor_decision::rejected: return "Rejected at " + c.decided_at;
        default: return c.approve_pending ? "Not decided: verifying the bytes before approving" : "Not decided";
    }
}

// Why the operator cannot pass manual check `id` now; nullptr when they can.
const char *rv_editor_why_not_pass(const rv_editor_app &app, const rv_editor_candidate &c, size_t id)
{
    if (c.bytes_changed || c.sha256.empty()) {
        return "These bytes are not the ones built";
    }
    if (c.runs.empty()) {
        return "Run the candidate first: a result is for what was seen playing it";
    }
    if (id != rv_editor_check_exit) {
        return nullptr;
    }
    if (app.session.live() && app.release.playing >= 0) {
        return "Leave the game first, by its own way out or Stop";
    }
    if (!c.last_run_clean) {
        return "The last run did not end cleanly, so it shows no clean exit";
    }
    return nullptr;
}

void rv_editor_check_row(rv_editor_app &app, rv_editor_candidate &c, size_t id, const rv_editor_theme &theme)
{
    rv_editor_check &check = c.checks[id];
    const bool valid = check.state == rv_editor_check_state::not_run || rv_editor_check_valid(c, check);
    ImGui::PushID(static_cast<int>(id));
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(check.name);
    ImGui::SetItemTooltip("Passed means: %s", check.passes_when);
    ImGui::TableNextColumn();
    const std::string state = valid ? rv_editor_check_state_name(check.state)
                                    : std::string(rv_editor_check_state_name(check.state)) + ", void: other bytes";
    rv_editor_status(state.c_str(), rv_editor_check_lamp(c, check), theme);
    if (!check.at.empty()) {
        ImGui::SetItemTooltip("%s\nset %s, on %s", check.note.c_str(), check.at.c_str(), check.env.c_str());
    }
    ImGui::TableNextColumn();
    if (!check.manual) {
        ImGui::TextUnformatted("by the editor");
        ImGui::PopID();
        return;
    }
    const std::string env = app.tools.console.path.string() +
        (app.session.facts().pdk.empty() ? "" : ", PDK " + app.session.facts().pdk);
    const char *why_not_pass = rv_editor_why_not_pass(app, c, id);
    const char *why_not_any = c.bytes_changed ? "These bytes are not the ones built" : nullptr;
    if (rv_editor_button("Pass", theme, { rv_editor_look::live, why_not_pass })) {
        rv_editor_check_set(c, id, rv_editor_check_state::passed, "seen by the operator: " + c.last_run_end, env);
    }
    rv_editor_flow(rv_editor_button_width("Fail"));
    if (rv_editor_button("Fail", theme, { rv_editor_look::live, why_not_any })) {
        rv_editor_check_set(c, id, rv_editor_check_state::failed, "failed as seen by the operator", env);
    }
    rv_editor_flow(rv_editor_button_width("Blocked"));
    if (rv_editor_button("Blocked", theme, { rv_editor_look::live, why_not_any })) {
        rv_editor_check_set(c, id, rv_editor_check_state::blocked, "could not be checked", env);
    }
    rv_editor_flow(rv_editor_button_width("Skip"));
    if (rv_editor_button("Skip", theme, { rv_editor_look::live, why_not_any })) {
        rv_editor_check_set(c, id, rv_editor_check_state::skipped, "skipped by the operator", env);
    }
    rv_editor_flow(rv_editor_button_width("Reset"));
    if (rv_editor_button("Reset", theme)) {
        rv_editor_check_set(c, id, rv_editor_check_state::not_run, "", env);
    }
    ImGui::PopID();
}

} // namespace

void rv_editor_pane_release_controls(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    if (rv_editor_tool_button("Build Candidate", rv_editor_glyph::build, 0xfab387, nullptr, theme,
            { rv_editor_look::live, rv_editor_app_why_not_build(app) })) {
        rv_editor_app_build_candidate(app);
    }
    rv_editor_flow(rv_editor_tool_button_width("Run Candidate"));
    if (rv_editor_tool_button("Run Candidate", rv_editor_glyph::run, theme.code_green, nullptr, theme,
            { rv_editor_look::live, rv_editor_app_why_not_run_candidate(app) })) {
        rv_editor_app_run_candidate(app);
    }
    rv_editor_flow(rv_editor_tool_button_width("Stop"));
    if (rv_editor_tool_button("Stop", rv_editor_glyph::stop, theme.code_red, "Shift+F5", theme,
            { rv_editor_look::live, rv_editor_app_why_not_stop(app) })) {
        rv_editor_app_stop(app);
    }
    rv_editor_flow(rv_editor_button_width("Export Report"));
    const char *why_not_export = app.release.candidates.empty() ? "No candidate to report on" : nullptr;
    if (rv_editor_button("Export Report", theme, { rv_editor_look::live, why_not_export })) {
        rv_editor_app_export_report(app);
    }

    std::string facts = "No candidate yet";
    if (!app.release.candidates.empty()) {
        const rv_editor_candidate &c = app.release.candidates[app.release.selected];
        facts = "Candidate #" + std::to_string(c.number) + " | " +
            (c.sha256.empty() ? std::string("hashing") : "sha256 " + c.sha256.substr(0, 16)) + " | " +
            rv_editor_checks_summary(c) + " | " + rv_editor_decision_text(c);
    }
    if (app.build.busy()) {
        facts = std::string("Building ") + (app.release.building ? "candidate" : "a development build") + " | " + facts;
    }
    rv_editor_flow(ImGui::CalcTextSize(facts.c_str()).x);
    ImGui::TextUnformatted(facts.c_str());
    if (app.build.busy() && rv_editor_button("Cancel Build", theme)) {
        app.build.cancel();
    }
    if (app.session.hung() && rv_editor_button("Force Stop", theme)) {
        app.session.force_stop(app.log);
    }
}

void rv_editor_pane_candidate(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_release &r = app.release;
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    if (!r.last_failure.empty()) {
        rv_editor_status(r.last_failure.c_str(), rv_editor_status_kind::error, theme);
    }
    if (r.candidates.empty()) {
        ImGui::TextWrapped("No candidate built in this window. Build Candidate has mppcburner write a disc image "
                           "under a new number; the checks below are then kept for exactly its bytes.");
        rv_editor_dim_text("Candidates and their checks live in this window's memory; the images stay in " +
            (app.project.cache_dir / "candidates").string() + ". Export Report writes a summary next to them.");
        return;
    }

    // Older candidates stay selectable: a new build never replaces one silently.
    if (r.candidates.size() > 1) {
        std::vector<std::string> names;
        for (const rv_editor_candidate &c : r.candidates) {
            names.push_back("Candidate #" + std::to_string(c.number));
        }
        std::vector<const char *> items;
        for (const std::string &n : names) {
            items.push_back(n.c_str());
        }
        int selected = static_cast<int>(r.selected);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
        if (rv_editor_dropdown("Shown", &selected, items.data(), static_cast<int>(items.size()), theme)) {
            r.selected = static_cast<size_t>(selected);
        }
    }
    rv_editor_candidate &c = r.candidates[r.selected];
    if (r.selected + 1 < r.candidates.size()) {
        rv_editor_status("A newer candidate exists; this one is older", rv_editor_status_kind::warning, theme);
    }
    if (c.bytes_changed) {
        rv_editor_status("The image's bytes changed after it was built: no result below applies to it",
            rv_editor_status_kind::error, theme);
    }

    if (ImGui::BeginTable("##identity", 2, ImGuiTableFlags_SizingStretchProp)) {
        rv_editor_row("Candidate", "#" + std::to_string(c.number) + ", built " + c.built_at);
        rv_editor_row("SHA-256", c.sha256.empty() ? (c.hash_error.empty() ? "hashing" : c.hash_error) : c.sha256);
        rv_editor_row("Size", std::to_string(c.size) + " bytes");
        rv_editor_row("Verified", c.hashing.valid() ? "reading the bytes again"
                : c.verified_hash == c.sha256       ? "same bytes at " + c.verified_at
                                                    : "different bytes at " + c.verified_at);
        rv_editor_row("Built by", "mppcburner build -o, the whole command on hover");
        ImGui::SetItemTooltip("%s", c.command.c_str());
        rv_editor_row("Tools", c.burner + "; " + c.baker);
        rv_editor_row("Sources", c.tree_changed ? "changed since this build started: this candidate stays as built"
                                                : "no change seen since this build started");
        const bool playing = app.session.live() && r.playing >= 0 && static_cast<size_t>(r.playing) == r.selected;
        rv_editor_row("Playtest", playing ? "session #" + std::to_string(app.session.number()) + ", " +
                    rv_editor_run_state_name(app.session.state()) + ", on the development console"
                : c.runs.empty() ? std::string("not run yet")
                                 : std::to_string(c.runs.size()) + " run(s); last ended: " + c.last_run_end);
        ImGui::EndTable();
    }
    rv_editor_path_row("Image", c.image.string(), theme);
    if (rv_editor_button("Verify Bytes", theme, { rv_editor_look::live, c.hashing.valid() ? "Already reading" : nullptr })) {
        rv_editor_candidate_hash(c);
    }

    ImGui::SeparatorText("Checks");
    rv_editor_dim_text("All seven are required and Skipped does not count. They cover only what each says (hover a "
                       "name), not the whole game.");
    if (ImGui::BeginTable("##checks", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Result");
        ImGui::TableHeadersRow();
        for (size_t id = 0; id < c.checks.size(); ++id) {
            rv_editor_check_row(app, c, id, theme);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Decision");
    rv_editor_status("Build succeeded", rv_editor_status_kind::ok, theme);
    const bool all = rv_editor_why_not_approve(c) == nullptr || c.decision == rv_editor_decision::approved;
    rv_editor_status(rv_editor_checks_summary(c).c_str(), all ? rv_editor_status_kind::ok : rv_editor_status_kind::warning,
        theme);
    const std::string decision = rv_editor_decision_text(c);
    rv_editor_status(decision.c_str(),
        c.decision == rv_editor_decision::approved       ? rv_editor_status_kind::ok
            : c.decision == rv_editor_decision::rejected ? rv_editor_status_kind::error
                                                         : rv_editor_status_kind::idle,
        theme);
    if (rv_editor_button("Approve", theme, { rv_editor_look::live, rv_editor_why_not_approve(c) })) {
        c.approve_pending = true;
        rv_editor_candidate_hash(c);
    }
    ImGui::SameLine();
    if (rv_editor_button("Reject", theme)) {
        c.decision = rv_editor_decision::rejected;
        c.decided_at = rv_editor_wall_clock();
    }
    ImGui::SameLine();
    if (rv_editor_button("Undecide", theme)) {
        c.decision = rv_editor_decision::none;
        c.decided_at.clear();
    }
    if (!r.report.empty()) {
        rv_editor_path_row("Report", r.report, theme);
    }
    if (!r.error.empty()) {
        rv_editor_status(r.error.c_str(), rv_editor_status_kind::error, theme);
    }
}

} // namespace rv_editor
