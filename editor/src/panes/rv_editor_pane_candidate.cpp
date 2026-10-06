// Release: the controls strip and the Candidate pane - which disc image is
// being checked, what was checked on its bytes, and the decision.

#include "panes/rv_editor_panes.hpp"

#include <string>
#include <vector>

#include "imgui.h"

#include "rv_editor_catppuccin_mocha.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_glyphs.hpp"
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

// One reserved single line, full row width: `text` clips rather than wraps,
// so this row can never split into two. Hovering shows it in full.
void rv_editor_reserved_line(const char *id, const std::string &text)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild(id, ImVec2(0.0f, ImGui::GetFrameHeight()), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (!text.empty()) {
        ImGui::TextUnformatted(text.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (!text.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text.c_str());
    }
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
        return rv_editor_text("pane_candidate.decision_void");
    }
    // Build succeeded, checks passed and approved are three facts.
    switch (c.decision) {
    case rv_editor_decision::approved: {
        const auto &op = c.operator_name;
        const auto &dt = c.decided_at;
        const auto &sha = c.sha256.substr(0, 12);
        return rv_editor_text_format("pane_candidate.approved_for_release",
            std::make_format_args(op, dt, sha));
    }
    case rv_editor_decision::rejected: {
        const auto &op = c.operator_name;
        const auto &dt = c.decided_at;
        return rv_editor_text_format("pane_candidate.rejected_by",
            std::make_format_args(op, dt));
    }
    default:
        break;
    }
    if (c.approve_pending) {
        return rv_editor_text("pane_candidate.awaiting_approval_verifying");
    }
    if (rv_editor_why_not_approve(c) == nullptr) {
        return rv_editor_text("pane_candidate.awaiting_approval");
    }
    return rv_editor_text("pane_candidate.not_ready");
}

// Why the operator cannot pass manual check `id` now; nullptr when they can.
const char *rv_editor_why_not_pass(const rv_editor_app &app, const rv_editor_candidate &c, size_t id)
{
    if (c.bytes_changed || c.sha256.empty()) {
        return rv_editor_text("pane_candidate.bytes_not_ones_built");
    }
    if (c.playtests == 0) {
        return rv_editor_text("pane_candidate.run_candidate_first");
    }
    if (id != rv_editor_check_exit) {
        return nullptr;
    }
    if (app.session.live() && app.release.playing >= 0) {
        return rv_editor_text("pane_candidate.leave_game_first");
    }
    if (!c.last_run_clean) {
        return rv_editor_text("pane_candidate.last_run_not_clean");
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
    const char *passed_means = rv_editor_text("pane_candidate.passed_means");
    ImGui::SetItemTooltip("%s %s", passed_means, check.passes_when);
    ImGui::TableNextColumn();
    const std::string state = valid ? rv_editor_check_state_name(check.state)
                                    : std::string(rv_editor_check_state_name(check.state)) + ", void: other bytes";
    rv_editor_status(state.c_str(), rv_editor_check_lamp(c, check), theme);
    if (!check.at.empty()) {
        ImGui::SetItemTooltip("%s\nset %s, on %s", check.note.c_str(), check.at.c_str(), check.env.c_str());
    }
    ImGui::TableNextColumn();
    if (!check.manual) {
        ImGui::TextUnformatted(rv_editor_text("pane_candidate.by_editor"));
        ImGui::PopID();
        return;
    }
    const std::string env = app.tools.console.path.string() +
        (app.session.facts().pdk.empty() ? "" : ", PDK " + app.session.facts().pdk);
    const char *why_not_pass = rv_editor_why_not_pass(app, c, id);
    const char *why_not_any = c.bytes_changed ? rv_editor_text("pane_candidate.bytes_not_ones_built") : nullptr;
    if (rv_editor_button(rv_editor_text("pane_candidate.pass_button"), theme, { rv_editor_look::live, why_not_pass })) {
        rv_editor_check_set(c, id, rv_editor_check_state::passed, "seen by the operator: " + c.last_run_end, env);
    }
    rv_editor_flow(rv_editor_button_width(rv_editor_text("pane_candidate.fail_button")));
    if (rv_editor_button(rv_editor_text("pane_candidate.fail_button"), theme, { rv_editor_look::live, why_not_any })) {
        rv_editor_check_set(c, id, rv_editor_check_state::failed, "failed as seen by the operator", env);
    }
    rv_editor_flow(rv_editor_button_width(rv_editor_text("pane_candidate.blocked_button")));
    if (rv_editor_button(rv_editor_text("pane_candidate.blocked_button"), theme, { rv_editor_look::live, why_not_any })) {
        rv_editor_check_set(c, id, rv_editor_check_state::blocked, "could not be checked", env);
    }
    rv_editor_flow(rv_editor_button_width(rv_editor_text("pane_candidate.skip_button")));
    if (rv_editor_button(rv_editor_text("pane_candidate.skip_button"), theme, { rv_editor_look::live, why_not_any })) {
        rv_editor_check_set(c, id, rv_editor_check_state::skipped, "skipped by the operator", env);
    }
    rv_editor_flow(rv_editor_button_width(rv_editor_text("pane_candidate.reset_button")));
    if (rv_editor_button(rv_editor_text("pane_candidate.reset_button"), theme)) {
        rv_editor_check_set(c, id, rv_editor_check_state::not_run, "", env);
    }
    ImGui::PopID();
}

// Every check of `c` with its state, result and the operator's buttons.
void rv_editor_candidate_checks(rv_editor_app &app, rv_editor_candidate &c, const rv_editor_theme &theme)
{
    const int check_count = static_cast<int>(c.checks.size());
    std::string help_text = rv_editor_text_format("pane_candidate.checks_help",
        std::make_format_args(check_count));
    rv_editor_dim_text(help_text);
    if (ImGui::BeginTable("##checks", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn(rv_editor_text("pane_candidate.check_column"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(rv_editor_text("pane_candidate.state_column"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(rv_editor_text("pane_candidate.result_column"));
        ImGui::TableHeadersRow();
        for (size_t id = 0; id < c.checks.size(); ++id) {
            rv_editor_check_row(app, c, id, theme);
        }
        ImGui::EndTable();
    }
}

} // namespace

void rv_editor_pane_checks(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    // No controls row of its own: the whole pane is content, so only a well.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const auto body = [&]() {
        rv_editor_release &r = app.release;
        if (r.candidates.empty()) {
            ImGui::TextWrapped("%s", rv_editor_text("pane_candidate.no_candidate_yet"));
            return;
        }
        rv_editor_candidate &c = r.candidates[r.selected];
        const int candidate_number = c.number;
        const auto summary = rv_editor_checks_summary(c);
        std::string what = rv_editor_text_format("pane_candidate.candidate_header",
            std::make_format_args(candidate_number, summary));
        ImGui::TextUnformatted(what.c_str());
        rv_editor_candidate_checks(app, c, theme);
    };
    body();
    rv_editor_well_end();
}

void rv_editor_pane_release_controls(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    // Only controls, no separate content: the whole pane is a shelf.
    rv_editor_shelf_begin("##shelf", theme);
    if (rv_editor_tool_button(rv_editor_text("pane_candidate.build_button"), rv_editor_glyph::build,
            rv_editor_mocha_peach, rv_editor_text("pane_candidate.build_candidate_tooltip"), nullptr, theme,
            { rv_editor_look::live, rv_editor_app_why_not_build(app) })) {
        rv_editor_app_build_candidate(app);
    }
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_candidate.run_button")));
    if (rv_editor_tool_button(rv_editor_text("pane_candidate.run_button"), rv_editor_glyph::run,
            theme.code_green, rv_editor_text("pane_candidate.run_candidate_tooltip"), nullptr, theme,
            { rv_editor_look::live, rv_editor_app_why_not_run_candidate(app) })) {
        rv_editor_app_run_candidate(app);
    }
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_candidate.player_button")));
    if (rv_editor_tool_button(rv_editor_text("pane_candidate.player_button"), rv_editor_glyph::run_in_player,
            rv_editor_mocha_mauve, rv_editor_text("pane_candidate.run_in_player_tooltip"), nullptr, theme,
            { rv_editor_look::live, rv_editor_app_why_not_play(app) })) {
        rv_editor_app_play_candidate(app);
    }
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_candidate.stop_button")));
    const bool session_live = app.session.live();
    const bool player_running = rv_editor_app_player_running(app);
    const char *why_not_stop_release = player_running ? nullptr : rv_editor_app_why_not_stop(app);
    if (rv_editor_tool_button(rv_editor_text("pane_candidate.stop_button"), rv_editor_glyph::stop,
            theme.code_red, rv_editor_text("pane_candidate.stop_tooltip"), nullptr, theme,
            { rv_editor_look::live, why_not_stop_release })) {
        if (session_live) {
            app.session.stop(app.log);
        }
        rv_editor_app_stop_player(app);
    }
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_candidate.report_button")));
    const char *why_not_export = nullptr;
    if (app.release.candidates.empty()) {
        why_not_export = rv_editor_text("pane_candidate.no_candidate_to_report");
    }
    if (rv_editor_tool_button(rv_editor_text("pane_candidate.report_button"), rv_editor_glyph::export_,
            theme.code_blue, rv_editor_text("pane_candidate.export_report_tooltip"), nullptr, theme,
            { rv_editor_look::live, why_not_export })) {
        rv_editor_app_export_report(app);
    }

    std::string facts = rv_editor_text("pane_candidate.no_candidate_yet_facts");
    if (!app.release.candidates.empty()) {
        const rv_editor_candidate &c = app.release.candidates[app.release.selected];
        const int candidate_number = c.number;
        const auto summary = rv_editor_checks_summary(c);
        const auto decision = rv_editor_decision_text(c);
        if (c.sha256.empty()) {
            facts = rv_editor_text_format("pane_candidate.candidate_facts_hashing",
                std::make_format_args(candidate_number, summary, decision));
        } else {
            const auto sha_short = c.sha256.substr(0, 16);
            facts = rv_editor_text_format("pane_candidate.candidate_facts_sha256",
                std::make_format_args(candidate_number, sha_short, summary, decision));
        }
    }
    if (app.build.busy()) {
        if (app.release.building) {
            facts = rv_editor_text_format("pane_candidate.building_candidate", std::make_format_args(facts));
        } else {
            facts = rv_editor_text_format("pane_candidate.building_development", std::make_format_args(facts));
        }
    }
    // Own row, clipped rather than flowed after the buttons: its length varies
    // with state, so joining the button row would make that state-dependent too.
    rv_editor_reserved_line("##facts", facts);
    // One action row shared by the two: a hung runtime needs Force Stop first,
    // a gap standing in when neither applies, so the strip's height stays fixed.
    if (app.session.hung()) {
        if (rv_editor_button(rv_editor_text("pane_candidate.force_stop_button"), theme)) {
            app.session.force_stop(app.log);
        }
    } else if (app.build.busy()) {
        if (rv_editor_button(rv_editor_text("pane_candidate.cancel_build_button"), theme)) {
            app.build.cancel();
        }
    } else {
        ImGui::Dummy(ImVec2(1.0f, ImGui::GetFrameHeight()));
    }
    rv_editor_shelf_end();
}

void rv_editor_pane_candidate(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_release &r = app.release;
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    // No controls row at the top: buttons sit inside the content between its
    // sections, so the whole pane is one well.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const auto body = [&]() {
        if (!r.last_failure.empty()) {
            rv_editor_status(r.last_failure.c_str(), rv_editor_status_kind::error, theme);
        }
        if (r.candidates.empty()) {
            ImGui::TextWrapped("No candidate yet. Build Candidate has mppcburner write a disc image under a new "
                               "number; the checks below are then kept for exactly its bytes.");
            rv_editor_dim_text("Each candidate's image, record, logs and reports are kept in " +
                (app.project.cache_dir / "candidates").string() + " and come back when the project opens.");
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
            rv_editor_row("Revision", c.source_revision.empty() ? "not recorded" : c.source_revision);
            rv_editor_row("Sources", c.tree_changed ? "changed since this build started: this candidate stays as built"
                                                    : "no change seen since this build started");
            const bool playing = app.session.live() && r.playing >= 0 && static_cast<size_t>(r.playing) == r.selected;
            rv_editor_row("Playtest", playing ? "session #" + std::to_string(app.session.number()) + ", " +
                        rv_editor_run_state_name(app.session.state()) + ", on the development console"
                    : c.playtests == 0 ? std::string("not run yet")
                                       : std::to_string(c.playtests) + " run(s); last ended: " + c.last_run_end);
            ImGui::EndTable();
        }
        rv_editor_path_row("Image", c.image.string(), theme);
        if (rv_editor_button("Verify Bytes", theme, { rv_editor_look::live, c.hashing.valid() ? "Already reading" : nullptr })) {
            rv_editor_candidate_hash(c);
        }

        ImGui::SeparatorText("Checks");
        rv_editor_candidate_checks(app, c, theme);

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
            c.operator_name = rv_editor_operator();
            c.dirty = true;
        }
        ImGui::SameLine();
        if (rv_editor_button("Undecide", theme)) {
            c.decision = rv_editor_decision::none;
            c.decided_at.clear();
            c.operator_name.clear();
            c.dirty = true;
        }
        if (!r.report.empty()) {
            rv_editor_path_row("Report", r.report, theme);
        }
        if (!r.error.empty()) {
            rv_editor_status(r.error.c_str(), rv_editor_status_kind::error, theme);
        }
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
