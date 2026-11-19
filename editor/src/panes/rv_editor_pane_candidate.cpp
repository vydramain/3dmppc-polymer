// Release Candidate pane: which disc image is being checked, what was checked on
// its bytes, and the decision.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "panes/rv_editor_pane_say.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_facts.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

namespace
{

// SHA-256 short display length for approved_for_release decision.
constexpr size_t sha256_approved_len = 12;

// Number of columns in checks table.
constexpr int checks_table_columns = 3;

// Cache subdirectory holding release candidates and their logs.
constexpr std::string_view candidates_dir_name = "candidates";

} // namespace

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

namespace
{

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

void rv_editor_check_row(rv_editor_app &app, rv_editor_candidate &c, size_t id, int &selected, const rv_editor_theme &theme)
{
    rv_editor_check &check = c.checks[id];
    const bool valid = check.state == rv_editor_check_state::not_run || rv_editor_check_valid(c, check);
    ImGui::PushID(static_cast<int>(id));
    rv_editor_table_row();
    rv_editor_table_cell();
    const rv_editor_table_row_result row = rv_editor_table_row_select("##check", selected == static_cast<int>(id), theme);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::AlignTextToFramePadding();
    rv_editor_label(check.name, rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
    rv_editor_item_tooltip((std::string(rv_editor_text("pane_candidate.passed_means")) + " " + check.passes_when).c_str());
    rv_editor_table_cell();
    const std::string state = valid ?
        rv_editor_check_state_name(check.state) :
        std::string(rv_editor_check_state_name(check.state)) + rv_editor_text("pane_candidate.void_suffix");
    rv_editor_status(state.c_str(), rv_editor_check_lamp(c, check), theme);
    if (!check.at.empty()) {
        rv_editor_item_tooltip((check.note + "\nset " + check.at + ", on " + check.env).c_str());
    }
    rv_editor_table_cell();
    if (row.clicked) {
        selected = static_cast<int>(id);
    }
    if (!check.manual) {
        rv_editor_label(rv_editor_text("pane_candidate.by_editor"),
            rv_editor_label_kind::normal,
            theme,
            rv_editor_label_layout::line);
        ImGui::PopID();
        return;
    }
    const std::string env =
        app.tools.console.path.string() + (app.session.facts().pdk.empty() ? "" : ", PDK " + app.session.facts().pdk);
    const char *why_not_pass = rv_editor_why_not_pass(app, c, id);
    const char *why_not_any = c.bytes_changed ? rv_editor_text("pane_candidate.bytes_not_ones_built") : nullptr;
    // A double click or Enter on a check that is not set does what Pass does, when Pass is allowed.
    const bool row_passes = row.activated && check.state == rv_editor_check_state::not_run && why_not_pass == nullptr;
    if (rv_editor_button(rv_editor_text("pane_candidate.pass_button"), theme, { rv_editor_look::live, why_not_pass }) ||
        row_passes) {
        rv_editor_check_set(c, id, rv_editor_check_state::passed, "seen by the operator: " + c.last_run_end, env);
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_candidate.fail_button"), theme, { rv_editor_look::live, why_not_any })) {
        rv_editor_check_set(c, id, rv_editor_check_state::failed, "failed as seen by the operator", env);
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_candidate.blocked_button"), theme, { rv_editor_look::live, why_not_any })) {
        rv_editor_check_set(c, id, rv_editor_check_state::blocked, "could not be checked", env);
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_candidate.skip_button"), theme, { rv_editor_look::live, why_not_any })) {
        rv_editor_check_set(c, id, rv_editor_check_state::skipped, "skipped by the operator", env);
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_candidate.reset_button"), theme)) {
        rv_editor_check_set(c, id, rv_editor_check_state::not_run, "", env);
    }
    ImGui::PopID();
}

// Every check of `c` with its state, result and the operator's buttons, as the rows of a table.
void rv_editor_candidate_checks(rv_editor_app &app, rv_editor_candidate &c, const rv_editor_theme &theme)
{
    // The state column is as wide as the widest state text, so no state moves what follows it.
    std::string widest;
    for (const auto state : { rv_editor_check_state::not_run,
             rv_editor_check_state::running,
             rv_editor_check_state::passed,
             rv_editor_check_state::failed,
             rv_editor_check_state::blocked,
             rv_editor_check_state::skipped }) {
        const std::string name = std::string(rv_editor_check_state_name(state)) + rv_editor_text("pane_candidate.void_suffix");
        widest = ImGui::CalcTextSize(name.c_str()).x > ImGui::CalcTextSize(widest.c_str()).x ? name : widest;
    }
    const float state_width =
        rv_editor_status_width(widest.c_str(), rv_editor_status_kind::idle, theme) + ImGui::GetStyle().CellPadding.x * 2.0f;
    const rv_editor_table_column columns[] = {
        { rv_editor_text("pane_candidate.check_column"), rv_editor_table_width::fixed },
        { rv_editor_text("pane_candidate.state_column"), rv_editor_table_width::fixed, state_width },
        { rv_editor_text("pane_candidate.result_column") },
    };
    // The selected row is this pane's own state, kept in the window's storage.
    ImGuiStorage *storage = ImGui::GetStateStorage();
    const ImGuiID selected_key = ImGui::GetID("##selected_check");
    int selected = storage->GetInt(selected_key, -1);
    if (rv_editor_table_begin("##checks",
            columns,
            checks_table_columns,
            { .row_bands = true, .header = true, .sizing = rv_editor_table_sizing::fixed_fit })) {
        for (size_t id = 0; id < c.checks.size(); ++id) {
            rv_editor_check_row(app, c, id, selected, theme);
        }
        rv_editor_table_end();
    }
    const std::string required = std::to_string(rv_editor_check_count);
    rv_editor_label(rv_editor_text_format("pane_candidate.checks_help", std::make_format_args(required)).c_str(),
        rv_editor_label_kind::dim,
        theme,
        rv_editor_label_layout::wrap);
    storage->SetInt(selected_key, selected);
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

namespace
{

// The widest row: the decision shelf, or the box with its button.
float rv_editor_candidate_content_width()
{
    return std::max(rv_editor_message_box_min_width({ { rv_editor_text("panes.open_project_box") } }),
        rv_editor_say_shelf_width({ rv_editor_text("pane_candidate.verify_bytes_button"),
            rv_editor_text("pane_candidate.approve_button"),
            rv_editor_text("pane_candidate.reject_button"),
            rv_editor_text("pane_candidate.undecide_button") }));
}

// Verify Bytes, Approve, Reject and Undecide in one row, always there; grey with the reason while there is no candidate.
void rv_editor_candidate_shelf(const rv_editor_app &app, rv_editor_candidate *c, const rv_editor_theme &theme)
{
    const char *none_why = !app.project.open ? rv_editor_text("panes.no_project_open_message") :
        c == nullptr                         ? rv_editor_text("pane_candidate.no_candidate_yet") :
                                               nullptr;
    rv_editor_shelf_begin("##shelf", theme);
    const char *verify_why = none_why != nullptr ? none_why :
        c->hashing.valid()                       ? rv_editor_text("pane_candidate.already_verifying") :
                                                   nullptr;
    if (rv_editor_button(rv_editor_text("pane_candidate.verify_bytes_button"), theme, { rv_editor_look::live, verify_why })) {
        rv_editor_candidate_hash(*c);
    }
    ImGui::SameLine();
    const char *approve_why = none_why != nullptr ? none_why : rv_editor_why_not_approve(*c);
    if (rv_editor_button(rv_editor_text("pane_candidate.approve_button"), theme, { rv_editor_look::live, approve_why })) {
        c->approve_pending = true;
        rv_editor_candidate_hash(*c);
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_candidate.reject_button"), theme, { rv_editor_look::live, none_why })) {
        c->decision = rv_editor_decision::rejected;
        c->decided_at = rv_editor_wall_clock();
        c->operator_name = rv_editor_operator();
        c->dirty = true;
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_candidate.undecide_button"), theme, { rv_editor_look::live, none_why })) {
        c->decision = rv_editor_decision::none;
        c->decided_at.clear();
        c->operator_name.clear();
        c->dirty = true;
    }
    rv_editor_shelf_end();
}

// What the box says: a failure, a refusal, that the bytes changed or that a newer candidate exists, else where the
// candidates are kept while there is none.
void rv_editor_candidate_say(rv_editor_app &app, const rv_editor_candidate *c, const rv_editor_theme &theme, float width)
{
    const rv_editor_release &r = app.release;
    std::string text;
    auto kind = rv_editor_label_kind::error;
    for (const std::string *part : { &r.last_failure, &r.error }) {
        text += part->empty() || text.empty() ? *part : "\n" + *part;
    }
    if (text.empty() && c != nullptr && c->bytes_changed) {
        text = rv_editor_text("pane_candidate.bytes_changed");
    } else if (text.empty() && c != nullptr && r.selected + 1 < r.candidates.size()) {
        text = rv_editor_text("pane_candidate.newer_candidate");
        kind = rv_editor_label_kind::warning;
    } else if (text.empty() && c == nullptr) {
        const auto dir = (app.project.cache_dir / std::string(candidates_dir_name)).string();
        text = rv_editor_text_format("pane_candidate.candidates_dir_note", std::make_format_args(dir));
        kind = rv_editor_label_kind::normal;
    }
    (void)rv_editor_message_box("##say", text, kind, {}, theme, width);
}

// The word and the lamp a record of the list shows for a candidate.
const char *rv_editor_candidate_status(const rv_editor_candidate &c, rv_editor_status_kind &kind)
{
    kind = rv_editor_status_kind::idle;
    if (c.bytes_changed) {
        kind = rv_editor_status_kind::error;
        return rv_editor_text("pane_candidate.status_changed");
    }
    if (c.decision == rv_editor_decision::approved) {
        kind = rv_editor_status_kind::ok;
        return rv_editor_text("pane_candidate.status_approved");
    }
    if (c.decision == rv_editor_decision::rejected) {
        kind = rv_editor_status_kind::error;
        return rv_editor_text("pane_candidate.status_rejected");
    }
    return rv_editor_text("pane_candidate.status_undecided");
}

// The candidates as records of two lines with a status; a click shows one.
void rv_editor_candidate_list(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_release &r = app.release;
    if (r.candidates.empty() && app.project.open) {
        rv_editor_content_empty_row(rv_editor_text("pane_candidate.no_candidate_yet_candidate_pane"), theme);
    }
    const char *const words[] = { rv_editor_text("pane_candidate.status_changed"),
        rv_editor_text("pane_candidate.status_approved"),
        rv_editor_text("pane_candidate.status_rejected"),
        rv_editor_text("pane_candidate.status_undecided") };
    const rv_editor_record_style widest = { .status_width = rv_editor_record_status_width(words, 4) };
    for (size_t i = 0; app.project.open && i < r.candidates.size(); ++i) {
        const rv_editor_candidate &c = r.candidates[i];
        rv_editor_status_kind kind;
        const char *word = rv_editor_candidate_status(c, kind);
        const auto num = std::to_string(c.number);
        const std::string name =
            rv_editor_text_format("pane_candidate.candidate_with_date", std::make_format_args(num, c.built_at));
        rv_editor_record_style style = widest;
        style.selected = i == r.selected;
        ImGui::PushID(static_cast<int>(i));
        if (rv_editor_record_row("##rec", name.c_str(), c.image.string().c_str(), word, kind, theme, style).clicked) {
            r.selected = i;
        }
        ImGui::PopID();
    }
}

// What is known of the shown candidate, as the facts list rows, whole.
void rv_editor_candidate_facts(const rv_editor_app &app, const rv_editor_candidate &c, const rv_editor_theme &theme)
{
    const rv_editor_release &r = app.release;
    const auto fact = [&theme](const char *key, const std::string &value) {
        rv_editor_facts_row(key, value.c_str(), theme);
    };
    const auto num = std::to_string(c.number);
    fact(rv_editor_text("pane_candidate.candidate_row"),
        rv_editor_text_format("pane_candidate.candidate_with_date", std::make_format_args(num, c.built_at)));
    const char *default_hash = c.hash_error.empty() ? rv_editor_text("pane_candidate.verifying_hash") : c.hash_error.c_str();
    fact(rv_editor_text("pane_candidate.sha256_row"), c.sha256.empty() ? default_hash : c.sha256);
    fact(rv_editor_text("pane_candidate.size_row"), std::to_string(c.size) + " bytes");
    std::string verified;
    if (c.hashing.valid()) {
        verified = rv_editor_text("pane_candidate.verified_reading");
    } else if (c.verified_hash == c.sha256) {
        verified = rv_editor_text_format("pane_candidate.verified_same", std::make_format_args(c.verified_at));
    } else {
        verified = rv_editor_text_format("pane_candidate.verified_different", std::make_format_args(c.verified_at));
    }
    fact(rv_editor_text("pane_candidate.verified_row"), verified);
    fact(rv_editor_text("pane_candidate.built_by_row"), rv_editor_text("pane_candidate.built_by_note"));
    rv_editor_item_tooltip(c.command.c_str());
    fact(rv_editor_text("pane_candidate.tools_row"), c.burner + "; " + c.baker);
    fact(rv_editor_text("pane_candidate.revision_row"),
        c.source_revision.empty() ? rv_editor_text("pane_candidate.revision_not_recorded") : c.source_revision);
    fact(rv_editor_text("pane_candidate.sources_row"),
        rv_editor_text(c.tree_changed ? "pane_candidate.sources_changed" : "pane_candidate.sources_unchanged"));
    std::string playtest;
    if (app.session.live() && r.playing >= 0 && static_cast<size_t>(r.playing) == r.selected) {
        const auto session_num = std::to_string(app.session.number());
        const auto state = rv_editor_run_state_name(app.session.state());
        playtest = rv_editor_text_format("pane_candidate.playtest_live", std::make_format_args(session_num, state));
    } else if (c.playtests == 0) {
        playtest = rv_editor_text("pane_candidate.playtest_not_run");
    } else {
        const auto runs = std::to_string(c.playtests);
        playtest = rv_editor_text_format("pane_candidate.playtest_ended", std::make_format_args(runs, c.last_run_end));
    }
    fact(rv_editor_text("pane_candidate.playtest_row"), playtest);
    fact(rv_editor_text("pane_candidate.image_row"), c.image.string());
    fact(rv_editor_text("pane_candidate.report_row"), r.report);
    fact(rv_editor_text("pane_candidate.checks_separator"), rv_editor_checks_summary(c));
    fact(rv_editor_text("pane_candidate.decision_separator"), rv_editor_decision_text(c));
}

} // namespace

rv_editor_size rv_editor_candidate_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_candidate_content_width(),
        rv_editor_tiled_shelf_block() + rv_editor_tiled_box_block(),
        { rv_editor_tiled_floor(), rv_editor_tiled_floor(), rv_editor_tiled_floor() });
}

rv_editor_size rv_editor_checks_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_message_box_min_width({}), 0.0f);
}

void rv_editor_pane_checks(rv_editor_app &app, const rv_editor_theme &theme)
{
    (void)rv_editor_tiled_area_begin("##checks_area", rv_editor_message_box_min_width({}), theme);
    rv_editor_release &r = app.release;
    if (app.project.open && !r.candidates.empty()) {
        rv_editor_candidate_checks(app, r.candidates[r.selected], theme);
    } else {
        rv_editor_content_empty_row(
            rv_editor_text(app.project.open ? "pane_candidate.no_candidate_yet" : "panes.no_project_open_message"),
            theme);
    }
    rv_editor_tiled_area_end(theme);
}

void rv_editor_pane_candidate(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_release &r = app.release;
    rv_editor_candidate *c = app.project.open && !r.candidates.empty() ? &r.candidates[r.selected] : nullptr;
    const float width = rv_editor_candidate_content_width();
    rv_editor_candidate_shelf(app, c, theme);
    if (!rv_editor_say_no_project(app, theme, width)) {
        rv_editor_candidate_say(app, c, theme, width);
    }
    (void)rv_editor_tiled_area_begin("##list_area",
        width,
        theme,
        { rv_editor_tiled_floor(), rv_editor_tiled_floor(), rv_editor_tiled_floor() });
    rv_editor_candidate_list(app, theme);
    rv_editor_tiled_area_end(theme);
    (void)rv_editor_tiled_area_begin("##facts_area", width, theme, { rv_editor_tiled_floor(), rv_editor_tiled_floor() }, true);
    if (c != nullptr) {
        rv_editor_candidate_facts(app, *c, theme);
    }
    rv_editor_tiled_area_end(theme);
    (void)rv_editor_tiled_area_begin("##checks_area", width, theme);
    if (c != nullptr) {
        rv_editor_candidate_checks(app, *c, theme);
    }
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
