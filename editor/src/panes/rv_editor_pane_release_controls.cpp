// Release Controls strip: the transport template with five squares (Build, Run, Run in Player, Stop and Export Report), then
// the action slot (Force Stop or Cancel Build); under them all, a status with the facts and the first open check.

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

void rv_editor_pane_release_controls(rv_editor_app &app, const rv_editor_theme &theme)
{
    // Only controls, no separate content: the whole pane is a shelf.
    rv_editor_shelf_begin("##shelf", theme);

    const rv_editor_tile build_tile{ rv_editor_text("pane_candidate.build_button"),
        rv_editor_glyph::build,
        rv_editor_mocha_peach,
        rv_editor_text("pane_candidate.build_candidate_tooltip") };
    const rv_editor_tile run_tile{ rv_editor_text("pane_candidate.run_button"),
        rv_editor_glyph::run,
        theme.code_green,
        rv_editor_text("pane_candidate.run_candidate_tooltip") };
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

    const bool has_candidate = !app.release.candidates.empty();
    const rv_editor_candidate *shown = has_candidate ? &app.release.candidates[app.release.selected] : nullptr;
    rv_editor_release_building building = rv_editor_release_building::none;
    if (app.build.busy()) {
        building = app.release.building ? rv_editor_release_building::candidate : rv_editor_release_building::development;
    }
    const rv_editor_release_facts facts = rv_editor_release_facts_make(shown, building);

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
    // Right of the squares, two rows: the action slot (always there, outside the message row), then the message row.
    ImGui::SameLine();
    ImGui::BeginGroup();
    // A hung runtime needs Force Stop first.
    const char *action_label = rv_editor_text("pane_candidate.cancel_build_button");
    const char *action_why = rv_editor_text("pane_candidate.no_build_to_cancel");
    rv_editor_pane_request_kind action_kind = rv_editor_pane_request_kind::force_stop;
    if (app.session.hung()) {
        const bool force_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::force_stop);
        action_label = rv_editor_text("pane_candidate.force_stop_button");
        action_why = force_pending ? rv_editor_text("app_why_not.stop_pending") : nullptr;
    } else if (app.build.busy()) {
        const bool cancel_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::cancel_build);
        const bool cancelling = app.build.state() == rv_editor_build_state::cancelling;
        const char *cancelling_why = cancelling ? rv_editor_text("panes.already_cancelling") : nullptr;
        action_kind = rv_editor_pane_request_kind::cancel_build;
        action_why = cancel_pending ? rv_editor_text("app_why_not.cancel_pending") : cancelling_why;
    }
    const std::initializer_list<const char *> action_labels = { rv_editor_text("pane_candidate.force_stop_button"),
        rv_editor_text("pane_candidate.cancel_build_button") };
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("##action",
        ImVec2(rv_editor_button_width_for(action_labels), ImGui::GetFrameHeight()),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar);
    if (rv_editor_button_fixed(action_label, action_labels, theme, { rv_editor_look::live, action_why })) {
        (void)rv_editor_app_request(app, { action_kind });
    }
    // The reason shows at once, not after the hover delay: it is why the slot is greyed.
    if (action_why != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        rv_editor_tooltip(action_why);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    // The status under the squares and the slot, as wide as the tile; its minimum width wraps the longest facts into its two
    // lines.
    const std::string text = app.project.open ? facts.first_line : rv_editor_text("panes.status_no_project");
    const std::string tip = shown != nullptr && app.project.open ? rv_editor_release_number_tooltip(*shown) : std::string();
    rv_editor_strip_status(text, std::max(rv_editor_release_status_min_width(), ImGui::GetContentRegionAvail().x), theme, tip);
    rv_editor_shelf_end();
    // The shelf ends the tile: no spacing after it, as the height says.
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);
}

} // namespace rv_editor
