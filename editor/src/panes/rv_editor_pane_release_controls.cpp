// Release Controls strip: Build, Run, Run in Player, Stop, Export Report, the shown
// candidate's facts, and the Force Stop or Cancel Build action.

#include "panes/rv_editor_panes.hpp"

#include <string>

#include "imgui.h"

#include "rv_editor_catppuccin_mocha.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// SHA-256 short display length for candidate facts with hash.
constexpr size_t sha256_facts_len = 16;

// One reserved single line, full row width: `text` clips rather than wraps,
// so this row can never split into two. Hovering shows it in full.
void rv_editor_reserved_line(const char *id, const std::string &text, const rv_editor_theme &theme)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild(id, ImVec2(0.0f, ImGui::GetFrameHeight()), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (!text.empty()) {
        rv_editor_label(text.c_str(), rv_editor_label_kind::normal, theme);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (!text.empty() && ImGui::IsItemHovered()) {
        rv_editor_tooltip(text.c_str());
    }
}

} // namespace

void rv_editor_pane_release_controls(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    // Only controls, no separate content: the whole pane is a shelf.
    rv_editor_shelf_begin("##shelf", theme);
    if (rv_editor_tool_button(rv_editor_text("pane_candidate.build_button"),
            rv_editor_glyph::build,
            rv_editor_mocha_peach,
            rv_editor_text("pane_candidate.build_candidate_tooltip"),
            nullptr,
            theme,
            { rv_editor_look::live, rv_editor_app_why_not_build(app) })) {
        rv_editor_app_build_candidate(app);
    }
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_candidate.run_button")));
    if (rv_editor_tool_button(rv_editor_text("pane_candidate.run_button"),
            rv_editor_glyph::run,
            theme.code_green,
            rv_editor_text("pane_candidate.run_candidate_tooltip"),
            nullptr,
            theme,
            { rv_editor_look::live, rv_editor_app_why_not_run_candidate(app) })) {
        rv_editor_app_run_candidate(app);
    }
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_candidate.player_button")));
    if (rv_editor_tool_button(rv_editor_text("pane_candidate.player_button"),
            rv_editor_glyph::run_in_player,
            rv_editor_mocha_mauve,
            rv_editor_text("pane_candidate.run_in_player_tooltip"),
            nullptr,
            theme,
            { rv_editor_look::live, rv_editor_app_why_not_play(app) })) {
        rv_editor_app_play_candidate(app);
    }
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_candidate.stop_button")));
    const bool session_live = app.session.live();
    const bool player_running = rv_editor_app_player_running(app);
    const bool stop_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::stop);
    const char *why_not_stop_release = player_running ? nullptr : rv_editor_app_why_not_stop(app);
    const bool stop_asked = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::stop_player) || stop_pending;
    const char *stop_release_why = stop_asked ? rv_editor_text("app_why_not.stop_pending") : why_not_stop_release;
    if (rv_editor_tool_button(rv_editor_text("pane_candidate.stop_button"),
            rv_editor_glyph::stop,
            theme.code_red,
            rv_editor_text("pane_candidate.stop_tooltip"),
            nullptr,
            theme,
            { rv_editor_look::live, stop_release_why })) {
        if (session_live) {
            (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::stop });
        }
        if (player_running) {
            (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::stop_player });
        }
    }
    rv_editor_flow(rv_editor_tool_button_width(rv_editor_text("pane_candidate.report_button")));
    const char *why_not_export = nullptr;
    if (app.release.candidates.empty()) {
        why_not_export = rv_editor_text("pane_candidate.no_candidate_to_report");
    }
    if (rv_editor_tool_button(rv_editor_text("pane_candidate.report_button"),
            rv_editor_glyph::export_,
            theme.code_blue,
            rv_editor_text("pane_candidate.export_report_tooltip"),
            nullptr,
            theme,
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
            const auto sha_short = c.sha256.substr(0, sha256_facts_len);
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
    rv_editor_reserved_line("##facts", facts, theme);
    // One action row shared by the two: a hung runtime needs Force Stop first,
    // a gap standing in when neither applies, so the strip's height stays fixed.
    if (app.session.hung()) {
        const bool force_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::force_stop);
        const char *force_why = force_pending ? rv_editor_text("app_why_not.stop_pending") : nullptr;
        if (rv_editor_button(rv_editor_text("pane_candidate.force_stop_button"), theme, { rv_editor_look::live, force_why })) {
            (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::force_stop });
        }
    } else if (app.build.busy()) {
        const bool cancel_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::cancel_build);
        const bool cancelling = app.build.state() == rv_editor_build_state::cancelling;
        const char *cancelling_why = cancelling ? rv_editor_text("panes.already_cancelling") : nullptr;
        const char *cancel_why = cancel_pending ? rv_editor_text("app_why_not.cancel_pending") : cancelling_why;
        if (rv_editor_button(rv_editor_text("pane_candidate.cancel_build_button"),
                theme,
                { rv_editor_look::live, cancel_why })) {
            (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::cancel_build });
        }
    } else {
        ImGui::Dummy(ImVec2(1.0f, ImGui::GetFrameHeight()));
    }
    rv_editor_shelf_end();
}

} // namespace rv_editor
