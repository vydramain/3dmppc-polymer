// Release Controls strip: the transport template with five squares (Build, Run, Run in Player, Stop and Export Report), then
// Cancel Build and Force Stop; under them all, a status with the facts and the first open check.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

#include "imgui.h"

#include "panes/rv_editor_release_facts.hpp"
#include "release/rv_editor_candidate.hpp"
#include "rv_editor_catppuccin_mocha.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_status.hpp"
#include "ui/rv_editor_widgets_strip_status.hpp"
#include "ui/rv_editor_widgets_tile.hpp"

namespace rv_editor
{

rv_editor_size rv_editor_release_controls_minimum()
{
    const float width = std::ceil(rv_editor_release_min_width());
    return { static_cast<int32_t>(width), static_cast<int32_t>(std::ceil(rv_editor_release_height(width))) };
}

void rv_editor_pane_release_controls(rv_editor_app &app,
    const rv_editor_theme &theme,
    const rv_editor_transport_shortcuts &shortcuts)
{
    // Only controls, no separate content: the whole pane is a shelf.
    rv_editor_shelf_begin("##shelf", theme);

    const rv_editor_tile build_tile{ rv_editor_text("pane_candidate.build_button"),
        rv_editor_glyph::build,
        rv_editor_mocha_peach,
        rv_editor_text("pane_candidate.build_candidate_tooltip"),
        shortcuts.build.empty() ? nullptr : shortcuts.build.c_str() };
    const rv_editor_tile run_tile{ rv_editor_text("pane_candidate.run_button"),
        rv_editor_glyph::run,
        theme.code_green,
        rv_editor_text("pane_candidate.run_candidate_tooltip"),
        shortcuts.run.empty() ? nullptr : shortcuts.run.c_str() };
    const rv_editor_tile player_tile{ rv_editor_text("pane_candidate.player_button"),
        rv_editor_glyph::run_in_player,
        rv_editor_mocha_mauve,
        rv_editor_text("pane_candidate.run_in_player_tooltip") };
    const bool session_live = app.session.live();
    const bool player_running = rv_editor_app_player_running(app);
    const bool stop_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::stop);
    const char *why_not_stop_release = player_running ? nullptr : rv_editor_app_why_not_stop(app);
    const bool stop_asked = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::stop_player) || stop_pending;
    const char *stop_release_why = stop_asked ? rv_editor_text("app_why_not.stop_pending") : why_not_stop_release;
    const rv_editor_tile stop_tile{ rv_editor_text("pane_candidate.stop_button"),
        rv_editor_glyph::stop,
        theme.code_red,
        rv_editor_text("pane_candidate.stop_tooltip") };
    const char *why_not_export = nullptr;
    if (app.release.candidates.empty()) {
        why_not_export = rv_editor_text("pane_candidate.no_candidate_to_report");
    }
    const rv_editor_tile report_tile{ rv_editor_text("pane_candidate.report_button"),
        rv_editor_glyph::export_,
        theme.code_blue,
        rv_editor_text("pane_candidate.export_report_tooltip") };

    // One row of five squares whose size follows the window's width alone (as Runtime Controls'), so no state moves one.
    const float width = ImGui::GetWindowWidth();
    const rv_editor_tile_size size =
        rv_editor_transport_is_large(width) ? rv_editor_tile_size::large : rv_editor_tile_size::small;
    const float side = rv_editor_transport_side(width, rv_editor_release_column_min_width(), release_buttons);
    const auto draw_tile = [&](rv_editor_tile tile, const char *why) {
        tile.side = side;
        return rv_editor_tile_button(tile, size, theme, { rv_editor_look::live, why });
    };
    if (draw_tile(build_tile, rv_editor_app_why_not_build(app))) {
        rv_editor_app_build_candidate(app);
    }
    ImGui::SameLine();
    if (draw_tile(run_tile, rv_editor_app_why_not_run_candidate(app))) {
        rv_editor_app_run_candidate(app);
    }
    ImGui::SameLine();
    if (draw_tile(player_tile, rv_editor_app_why_not_play(app))) {
        rv_editor_app_play_candidate(app);
    }
    ImGui::SameLine();
    if (draw_tile(stop_tile, stop_release_why)) {
        if (session_live) {
            (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::stop });
        }
        if (player_running) {
            (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::stop_player });
        }
    }
    ImGui::SameLine();
    if (draw_tile(report_tile, why_not_export)) {
        rv_editor_app_export_report(app);
    }
    // Cancel Build and Force Stop keep their places beside the transport.
    ImGui::SameLine();
    const bool cancel_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::cancel_build);
    const bool cancelling = app.build.state() == rv_editor_build_state::cancelling;
    const char *cancelling_why = cancelling ? rv_editor_text("panes.already_cancelling") : nullptr;
    const char *busy_why = cancel_pending ? rv_editor_text("app_why_not.cancel_pending") : cancelling_why;
    const char *cancel_why = app.build.busy() ? busy_why : rv_editor_text("pane_candidate.no_build_to_cancel");
    if (rv_editor_button(rv_editor_text("pane_candidate.cancel_build_button"), theme, { rv_editor_look::live, cancel_why })) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::cancel_build });
    }
    if (cancel_why != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        rv_editor_tooltip(cancel_why);
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_candidate.force_stop_button"),
            theme,
            { rv_editor_look::live, rv_editor_app_why_not_force_stop(app) })) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::force_stop });
    }
    // The status under the squares and the slot, as wide as the tile; its minimum width wraps the longest facts into its two
    // lines.
    const float status_width = std::max(rv_editor_release_status_min_width(), ImGui::GetContentRegionAvail().x);
    std::string status = rv_editor_text("panes.status_no_project");
    std::string tip;
    if (app.project.open) {
        const auto &release = app.release;
        const size_t described =
            rv_editor_app_release_still_playing(app) ? static_cast<size_t>(release.playing) : release.selected;
        const rv_editor_candidate *shown = release.candidates.empty() ? nullptr : &release.candidates[described];
        const std::string facts = rv_editor_release_facts_make(shown, rv_editor_release_building::none).first_line;
        const std::string build = rv_editor_build_status(app.build);
        const auto join_status = [&](const std::string &first, const std::string &second) {
            const std::string combined = rv_editor_text_format("panes.status_join", std::make_format_args(first, second));
            if (ImGui::CalcTextSize(combined.c_str(), nullptr, false, status_width).y <= rv_editor_strip_status_height()) {
                status = combined;
            }
        };
        if (app.session.live()) {
            status = rv_editor_app_live_session_status(app);
            tip = status + "\n";
            if (!build.empty()) {
                status = rv_editor_text_format("panes.status_join", std::make_format_args(status, build));
                tip += build + "\n";
            }
            if (shown != nullptr && app.candidate_session != app.session.number()) {
                join_status(facts, status);
            }
            tip += facts + "\n";
        } else {
            status = build.empty() ? facts : build;
            if (!build.empty() && shown != nullptr) {
                join_status(build, facts);
                tip = build + "\n" + facts + "\n";
            }
        }
        if (shown != nullptr) {
            tip += rv_editor_release_number_tooltip(*shown);
        }
    }
    rv_editor_strip_status(status, status_width, theme, tip);
    rv_editor_shelf_end();
    // The shelf ends the tile: no spacing after it, as the height says.
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);
}

} // namespace rv_editor
