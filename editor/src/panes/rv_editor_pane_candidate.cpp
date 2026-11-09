// Release Candidate pane: which disc image is being checked, what was checked on
// its bytes, and the decision.

#include "panes/rv_editor_panes.hpp"

#include <string>
#include <vector>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// SHA-256 short display length for approved_for_release decision.
constexpr size_t sha256_approved_len = 12;

// Number of columns in checks table.
constexpr int checks_table_columns = 3;

// Number of columns in identity table.
constexpr int identity_table_columns = 2;

// Candidate selector dropdown width in em.
constexpr float candidate_dropdown_width_em = 16.0f;

// Cache subdirectory holding release candidates and their logs.
constexpr std::string_view candidates_dir_name = "candidates";

void rv_editor_row(const char *label, const std::string &value, const rv_editor_theme &theme)
{
    rv_editor_table_row();
    rv_editor_table_cell();
    rv_editor_label(label, rv_editor_label_kind::normal, theme);
    rv_editor_table_cell();
    rv_editor_label(value.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
}

rv_editor_status_kind rv_editor_check_lamp(const rv_editor_candidate &c, const rv_editor_check &check)
{
    if (check.state != rv_editor_check_state::not_run && !rv_editor_check_valid(c, check)) {
        return rv_editor_status_kind::error;
    }
    switch (check.state) {
    case rv_editor_check_state::passed:
        return rv_editor_status_kind::ok;
    case rv_editor_check_state::running:
        return rv_editor_status_kind::busy;
    case rv_editor_check_state::failed:
    case rv_editor_check_state::blocked:
        return rv_editor_status_kind::error;
    case rv_editor_check_state::skipped:
        return rv_editor_status_kind::warning;
    default:
        return rv_editor_status_kind::idle;
    }
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
    rv_editor_table_row();
    rv_editor_table_cell();
    rv_editor_label(check.name, rv_editor_label_kind::normal, theme);
    rv_editor_item_tooltip((std::string(rv_editor_text("pane_candidate.passed_means")) + " " + check.passes_when).c_str());
    rv_editor_table_cell();
    const std::string state = valid ? rv_editor_check_state_name(check.state) :
                                      std::string(rv_editor_check_state_name(check.state)) + ", void: other bytes";
    rv_editor_status(state.c_str(), rv_editor_check_lamp(c, check), theme);
    if (!check.at.empty()) {
        rv_editor_item_tooltip((check.note + "\nset " + check.at + ", on " + check.env).c_str());
    }
    rv_editor_table_cell();
    if (!check.manual) {
        rv_editor_label(rv_editor_text("pane_candidate.by_editor"), rv_editor_label_kind::normal, theme);
        ImGui::PopID();
        return;
    }
    const std::string env =
        app.tools.console.path.string() + (app.session.facts().pdk.empty() ? "" : ", PDK " + app.session.facts().pdk);
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
    std::string help_text = rv_editor_text_format("pane_candidate.checks_help", std::make_format_args(check_count));
    rv_editor_label(help_text.c_str(), rv_editor_label_kind::dim, theme, rv_editor_label_layout::wrap);
    const rv_editor_table_column columns[] = {
        { rv_editor_text("pane_candidate.check_column"), rv_editor_table_width::fixed },
        { rv_editor_text("pane_candidate.state_column"), rv_editor_table_width::fixed },
        { rv_editor_text("pane_candidate.result_column") },
    };
    if (rv_editor_table_begin("##checks", columns, checks_table_columns, { .row_bands = true, .header = true })) {
        for (size_t id = 0; id < c.checks.size(); ++id) {
            rv_editor_check_row(app, c, id, theme);
        }
        rv_editor_table_end();
    }
}

} // namespace

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
        const auto &sha = c.sha256.substr(0, sha256_approved_len);
        return rv_editor_text_format("pane_candidate.approved_for_release", std::make_format_args(op, dt, sha));
    }
    case rv_editor_decision::rejected: {
        const auto &op = c.operator_name;
        const auto &dt = c.decided_at;
        return rv_editor_text_format("pane_candidate.rejected_by", std::make_format_args(op, dt));
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
            rv_editor_label(rv_editor_text("pane_candidate.no_candidate_yet"),
                rv_editor_label_kind::normal,
                theme,
                rv_editor_label_layout::wrap);
            return;
        }
        rv_editor_candidate &c = r.candidates[r.selected];
        const int candidate_number = c.number;
        const auto summary = rv_editor_checks_summary(c);
        std::string what =
            rv_editor_text_format("pane_candidate.candidate_header", std::make_format_args(candidate_number, summary));
        rv_editor_label(what.c_str(), rv_editor_label_kind::normal, theme);
        rv_editor_candidate_checks(app, c, theme);
    };
    body();
    rv_editor_well_end();
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
            rv_editor_label(rv_editor_text("pane_candidate.no_candidate_yet_candidate_pane"),
                rv_editor_label_kind::normal,
                theme,
                rv_editor_label_layout::wrap);
            const auto dir = (app.project.cache_dir / std::string(candidates_dir_name)).string();
            std::string note = rv_editor_text_format("pane_candidate.candidates_dir_note", std::make_format_args(dir));
            rv_editor_label(note.c_str(), rv_editor_label_kind::dim, theme, rv_editor_label_layout::wrap);
            return;
        }

        // Older candidates stay selectable: a new build never replaces one silently.
        if (r.candidates.size() > 1) {
            std::vector<std::string> names;
            for (const rv_editor_candidate &c : r.candidates) {
                const auto num = std::to_string(c.number);
                names.push_back(rv_editor_text_format("pane_candidate.candidate_number", std::make_format_args(num)));
            }
            std::vector<const char *> items;
            for (const std::string &n : names) {
                items.push_back(n.c_str());
            }
            int selected = static_cast<int>(r.selected);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * candidate_dropdown_width_em);
            if (rv_editor_dropdown(rv_editor_text("pane_candidate.shown_label"),
                    &selected,
                    items.data(),
                    static_cast<int>(items.size()),
                    theme)) {
                r.selected = static_cast<size_t>(selected);
            }
        }
        rv_editor_candidate &c = r.candidates[r.selected];
        if (r.selected + 1 < r.candidates.size()) {
            rv_editor_status(rv_editor_text("pane_candidate.newer_candidate"), rv_editor_status_kind::warning, theme);
        }
        if (c.bytes_changed) {
            rv_editor_status(rv_editor_text("pane_candidate.bytes_changed"), rv_editor_status_kind::error, theme);
        }

        const rv_editor_table_column identity_columns[identity_table_columns] = {};
        if (rv_editor_table_begin("##identity", identity_columns, identity_table_columns)) {
            const auto num = std::to_string(c.number);
            std::string candidate_info =
                rv_editor_text_format("pane_candidate.candidate_with_date", std::make_format_args(num, c.built_at));
            rv_editor_row(rv_editor_text("pane_candidate.candidate_row"), candidate_info, theme);
            const char *hash_verifying = rv_editor_text("pane_candidate.verifying_hash");
            const char *default_hash = c.hash_error.empty() ? hash_verifying : c.hash_error.c_str();
            const char *sha_value = c.sha256.empty() ? default_hash : c.sha256.c_str();
            rv_editor_row(rv_editor_text("pane_candidate.sha256_row"), sha_value, theme);
            rv_editor_row(rv_editor_text("pane_candidate.size_row"), std::to_string(c.size) + " bytes", theme);
            std::string verified_value;
            if (c.hashing.valid()) {
                verified_value = rv_editor_text("pane_candidate.verified_reading");
            } else if (c.verified_hash == c.sha256) {
                verified_value = rv_editor_text_format("pane_candidate.verified_same", std::make_format_args(c.verified_at));
            } else {
                verified_value =
                    rv_editor_text_format("pane_candidate.verified_different", std::make_format_args(c.verified_at));
            }
            rv_editor_row(rv_editor_text("pane_candidate.verified_row"), verified_value, theme);
            rv_editor_row(rv_editor_text("pane_candidate.built_by_row"), rv_editor_text("pane_candidate.built_by_note"), theme);
            rv_editor_item_tooltip(c.command.c_str());
            rv_editor_row(rv_editor_text("pane_candidate.tools_row"), c.burner + "; " + c.baker, theme);
            const char *default_revision = rv_editor_text("pane_candidate.revision_not_recorded");
            const char *revision_value = c.source_revision.empty() ? default_revision : c.source_revision.c_str();
            rv_editor_row(rv_editor_text("pane_candidate.revision_row"), revision_value, theme);
            const char *sources_text1 = rv_editor_text("pane_candidate.sources_changed");
            const char *sources_text2 = rv_editor_text("pane_candidate.sources_unchanged");
            std::string sources_value = c.tree_changed ? std::string(sources_text1) : std::string(sources_text2);
            rv_editor_row(rv_editor_text("pane_candidate.sources_row"), sources_value, theme);
            const bool playing = app.session.live() && r.playing >= 0 && static_cast<size_t>(r.playing) == r.selected;
            std::string playtest_value;
            if (playing) {
                const auto num = std::to_string(app.session.number());
                const auto state = rv_editor_run_state_name(app.session.state());
                playtest_value = rv_editor_text_format("pane_candidate.playtest_live", std::make_format_args(num, state));
            } else if (c.playtests == 0) {
                playtest_value = rv_editor_text("pane_candidate.playtest_not_run");
            } else {
                const auto num = std::to_string(c.playtests);
                playtest_value =
                    rv_editor_text_format("pane_candidate.playtest_ended", std::make_format_args(num, c.last_run_end));
            }
            rv_editor_row(rv_editor_text("pane_candidate.playtest_row"), playtest_value, theme);
            rv_editor_table_end();
        }
        rv_editor_path_row(rv_editor_text("pane_candidate.image_row"), c.image.string(), theme);
        const char *verify_disabled = c.hashing.valid() ? rv_editor_text("pane_candidate.already_verifying") : nullptr;
        if (rv_editor_button(rv_editor_text("pane_candidate.verify_bytes_button"),
                theme,
                { rv_editor_look::live, verify_disabled })) {
            rv_editor_candidate_hash(c);
        }

        rv_editor_section(rv_editor_text("pane_candidate.checks_separator"));
        rv_editor_candidate_checks(app, c, theme);

        rv_editor_section(rv_editor_text("pane_candidate.decision_separator"));
        rv_editor_status(rv_editor_text("pane_candidate.build_succeeded"), rv_editor_status_kind::ok, theme);
        const bool all = rv_editor_why_not_approve(c) == nullptr || c.decision == rv_editor_decision::approved;
        rv_editor_status(rv_editor_checks_summary(c).c_str(),
            all ? rv_editor_status_kind::ok : rv_editor_status_kind::warning,
            theme);
        const std::string decision = rv_editor_decision_text(c);
        rv_editor_status(decision.c_str(),
            c.decision == rv_editor_decision::approved     ? rv_editor_status_kind::ok :
                c.decision == rv_editor_decision::rejected ? rv_editor_status_kind::error :
                                                             rv_editor_status_kind::idle,
            theme);
        if (rv_editor_button(rv_editor_text("pane_candidate.approve_button"),
                theme,
                { rv_editor_look::live, rv_editor_why_not_approve(c) })) {
            c.approve_pending = true;
            rv_editor_candidate_hash(c);
        }
        ImGui::SameLine();
        if (rv_editor_button(rv_editor_text("pane_candidate.reject_button"), theme)) {
            c.decision = rv_editor_decision::rejected;
            c.decided_at = rv_editor_wall_clock();
            c.operator_name = rv_editor_operator();
            c.dirty = true;
        }
        ImGui::SameLine();
        if (rv_editor_button(rv_editor_text("pane_candidate.undecide_button"), theme)) {
            c.decision = rv_editor_decision::none;
            c.decided_at.clear();
            c.operator_name.clear();
            c.dirty = true;
        }
        if (!r.report.empty()) {
            rv_editor_path_row(rv_editor_text("pane_candidate.report_row"), r.report, theme);
        }
        if (!r.error.empty()) {
            rv_editor_status(r.error.c_str(), rv_editor_status_kind::error, theme);
        }
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
