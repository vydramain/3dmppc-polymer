// Release Controls strip: Build, Run, Run in Player, Stop and Export Report as tiles, then a
// column of two lines: the shown candidate's facts, and one line of the checks not yet passed
// followed by the Force Stop or Cancel Build action.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
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
#include "ui/rv_editor_widgets_tile.hpp"

namespace rv_editor
{

namespace
{

// One reserved single line, the rest of the row wide: `text` clips rather than wraps,
// so this row never splits into two. Hovering shows it in full.
void rv_editor_reserved_line(const char *id, const std::string &text, const std::string &tip, const rv_editor_theme &theme)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild(id, ImVec2(0.0f, ImGui::GetFrameHeight()), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (!text.empty()) {
        rv_editor_label(text.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (!text.empty() && ImGui::IsItemHovered()) {
        rv_editor_tooltip(tip.c_str());
    }
}

// Text of the "+N" chip: how many open checks the row has no room for.
std::string rv_editor_more_text(size_t rest)
{
    const int count = static_cast<int>(rest);
    return rv_editor_text_format("pane_candidate.more_not_passed", std::make_format_args(count));
}

// The checks of `c` that do not count as Passed, as chips while each still fits `width`.
// A "+N" chip takes the rest; its hover lists their names. Hovering a chip anywhere shows
// its note (what was seen) or, without one, what a Passed claims.
void rv_editor_checks_content(const rv_editor_candidate &c, float width, const rv_editor_theme &theme)
{
    const auto counts = [&c](const rv_editor_check &check) {
        return check.state == rv_editor_check_state::passed && rv_editor_check_valid(c, check);
    };
    if (std::all_of(c.checks.begin(), c.checks.end(), counts)) {
        rv_editor_label(rv_editor_text("pane_candidate.all_checks_passed"), rv_editor_label_kind::dim, theme);
        return;
    }
    std::vector<const rv_editor_check *> open;
    for (const rv_editor_check &check : c.checks) {
        if (!counts(check)) {
            open.push_back(&check);
        }
    }
    const char *label = rv_editor_text("pane_candidate.not_passed_label");
    rv_editor_label(label, rv_editor_label_kind::dim, theme, rv_editor_label_layout::line);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const size_t count = open.size();
    float used = ImGui::CalcTextSize(label).x;
    // Upper bound of the "+N" chip's width: its text at the full count is the widest it gets.
    const float more_room = gap + ImGui::CalcTextSize(rv_editor_more_text(count).c_str()).x;
    size_t shown = 0;
    for (; shown < count; ++shown) {
        const rv_editor_check &check = *open[shown];
        // A chip is kept only with room left for the "+N" chip when checks remain after it.
        const size_t rest = count - shown - 1;
        const float chip = gap + rv_editor_status_width(check.name, rv_editor_check_lamp(c, check), theme);
        const float more = rest == 0 ? 0.0f : more_room;
        if (used + chip + more > width) {
            break;
        }
        used += chip;
        ImGui::SameLine();
        ImGui::BeginGroup();
        rv_editor_status(check.name, rv_editor_check_lamp(c, check), theme);
        ImGui::EndGroup();
        rv_editor_item_tooltip(check.note.empty() ? check.passes_when : check.note.c_str());
    }
    if (shown < count) {
        std::string names;
        for (size_t i = shown; i < count; ++i) {
            if (!names.empty()) {
                names += '\n';
            }
            names += open[i]->name;
        }
        const std::string more = rv_editor_more_text(count - shown);
        ImGui::SameLine();
        ImGui::BeginGroup();
        rv_editor_label(more.c_str(), rv_editor_label_kind::dim, theme, rv_editor_label_layout::line);
        ImGui::EndGroup();
        rv_editor_item_tooltip(names.c_str());
    }
}

// Row two's checks part in one transparent child of `width`: nothing is drawn past the
// edge, and the slot button follows the child on the row.
void rv_editor_open_checks(const rv_editor_candidate &c, float width, const rv_editor_theme &theme)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("##checks",
        ImVec2(std::max(width, 1.0f), ImGui::GetFrameHeight()),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar);
    rv_editor_checks_content(c, width, theme);
    ImGui::EndChild();
    ImGui::PopStyleColor();
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
    const rv_editor_tile tiles[] = { build_tile, run_tile, player_tile, stop_tile, report_tile };

    const bool has_candidate = !app.release.candidates.empty();
    const rv_editor_candidate *shown = has_candidate ? &app.release.candidates[app.release.selected] : nullptr;
    rv_editor_release_building building = rv_editor_release_building::none;
    if (app.build.busy()) {
        building = app.release.building ? rv_editor_release_building::candidate : rv_editor_release_building::development;
    }
    const rv_editor_release_facts facts = rv_editor_release_facts_make(shown, building);

    // One size for the whole strip, chosen before the first tile is drawn.
    const rv_editor_tile_size size =
        rv_editor_release_tile_size(tiles, ImGui::CalcTextSize(facts.first_line.c_str()).x, ImGui::GetContentRegionAvail().x);
    const rv_editor_state build_state{ rv_editor_look::live, rv_editor_app_why_not_build(app) };
    if (rv_editor_tile_button(build_tile, size, theme, build_state)) {
        rv_editor_app_build_candidate(app);
    }
    ImGui::SameLine();
    const rv_editor_state run_state{ rv_editor_look::live, rv_editor_app_why_not_run_candidate(app) };
    if (rv_editor_tile_button(run_tile, size, theme, run_state)) {
        rv_editor_app_run_candidate(app);
    }
    ImGui::SameLine();
    const rv_editor_state player_state{ rv_editor_look::live, rv_editor_app_why_not_play(app) };
    if (rv_editor_tile_button(player_tile, size, theme, player_state)) {
        rv_editor_app_play_candidate(app);
    }
    ImGui::SameLine();
    const rv_editor_state stop_state{ rv_editor_look::live, stop_release_why };
    if (rv_editor_tile_button(stop_tile, size, theme, stop_state)) {
        if (session_live) {
            (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::stop });
        }
        if (player_running) {
            (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::stop_player });
        }
    }
    ImGui::SameLine();
    const rv_editor_state report_state{ rv_editor_look::live, why_not_export };
    if (rv_editor_tile_button(report_tile, size, theme, report_state)) {
        rv_editor_app_export_report(app);
    }
    // Right of the tiles, one column of two lines. The facts take the rest of line one and
    // clip, so a longer text cannot wrap it; the number's hover shows the sha256.
    ImGui::SameLine();
    ImGui::BeginGroup();
    const std::string tip = shown != nullptr ? rv_editor_release_number_tooltip(*shown) : facts.first_line;
    rv_editor_reserved_line("##facts", facts.first_line, tip, theme);

    // Line two: the action is chosen first, its width reserved, then the checks fill the rest.
    // A hung runtime needs Force Stop first.
    const char *action_label = nullptr;
    const char *action_why = nullptr;
    rv_editor_pane_request_kind action_kind = rv_editor_pane_request_kind::force_stop;
    if (app.session.hung()) {
        const bool force_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::force_stop);
        action_label = rv_editor_text("pane_candidate.force_stop_button");
        action_why = force_pending ? rv_editor_text("app_why_not.stop_pending") : nullptr;
    } else if (app.build.busy()) {
        const bool cancel_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::cancel_build);
        const bool cancelling = app.build.state() == rv_editor_build_state::cancelling;
        const char *cancelling_why = cancelling ? rv_editor_text("panes.already_cancelling") : nullptr;
        action_label = rv_editor_text("pane_candidate.cancel_build_button");
        action_kind = rv_editor_pane_request_kind::cancel_build;
        action_why = cancel_pending ? rv_editor_text("app_why_not.cancel_pending") : cancelling_why;
    }
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float action_room = action_label != nullptr ? rv_editor_button_width(action_label) + gap : 0.0f;
    if (facts.second_line) {
        rv_editor_open_checks(*shown, ImGui::GetContentRegionAvail().x - action_room, theme);
    }
    if (action_label != nullptr) {
        if (facts.second_line) {
            ImGui::SameLine();
        }
        if (rv_editor_button(action_label, theme, { rv_editor_look::live, action_why })) {
            (void)rv_editor_app_request(app, { action_kind });
        }
    }
    ImGui::EndGroup();
    rv_editor_shelf_end();
}

} // namespace rv_editor
