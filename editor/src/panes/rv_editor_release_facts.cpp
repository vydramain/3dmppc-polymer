// Release Controls: the first line beside the tiles, the sha256 hover and the tile size choice.

#include "panes/rv_editor_release_facts.hpp"

#include <algorithm>
#include <iterator>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_status.hpp"
#include "ui/rv_editor_widgets_strip_status.hpp"

namespace rv_editor
{

rv_editor_release_facts rv_editor_release_facts_make(const rv_editor_candidate *c, rv_editor_release_building building)
{
    rv_editor_release_facts facts;
    if (c == nullptr) {
        facts.first_line = rv_editor_text("pane_candidate.no_candidate_yet_facts");
    } else {
        const auto counts = [c](const rv_editor_check &check) {
            return check.state == rv_editor_check_state::passed && rv_editor_check_valid(*c, check);
        };
        const int passed = static_cast<int>(std::count_if(c->checks.begin(), c->checks.end(), counts));
        const int total = static_cast<int>(c->checks.size());
        const int number = c->number;
        const char *verdict = rv_editor_text(passed == total ? "pane_candidate.ready" : "pane_candidate.not_ready");
        facts.first_line =
            rv_editor_text_format("pane_candidate.candidate_facts", std::make_format_args(number, verdict, passed, total));
        const auto open = std::find_if_not(c->checks.begin(), c->checks.end(), counts);
        if (open != c->checks.end()) {
            facts.first_line =
                rv_editor_text_format("pane_candidate.not_passed_facts", std::make_format_args(facts.first_line, open->name));
            const int more = total - passed - 1;
            if (more > 0) {
                facts.first_line =
                    rv_editor_text_format("pane_candidate.not_passed_more", std::make_format_args(facts.first_line, more));
            }
        }
    }
    if (building != rv_editor_release_building::none) {
        const char *key = building == rv_editor_release_building::candidate ? "pane_candidate.building_candidate" :
                                                                              "pane_candidate.building_development";
        facts.first_line = rv_editor_text_format(key, std::make_format_args(facts.first_line));
    }
    return facts;
}

std::string rv_editor_release_number_tooltip(const rv_editor_candidate &c)
{
    if (c.sha256.empty()) {
        return rv_editor_text("pane_candidate.candidate_sha256_pending");
    }
    return rv_editor_text_format("pane_candidate.candidate_sha256_tooltip", std::make_format_args(c.sha256));
}

// The width the longest facts need to wrap into the status's two lines. Asked every frame; the answer changes only with the
// font or its size.
float rv_editor_release_status_min_width()
{
    static const ImFont *cached_font = nullptr;
    static float cached_size = 0.0f;
    static float cached_width = 0.0f;
    const ImFont *font = ImGui::GetIO().Fonts->Fonts[0];
    if (font == cached_font && font->LegacySize == cached_size) {
        return cached_width;
    }
    rv_editor_candidate longest; // the widest number: six digits
    longest.number = 999999;
    longest.sha256 = "abc"; // a result counts only for hashed bytes
    longest.checks = rv_editor_checks_make();
    float facts =
        rv_editor_strip_status_width_for(rv_editor_release_facts_make(nullptr, rv_editor_release_building::none).first_line);
    // Every verdict, with the first open check at each place, and the building prefixes, wrapped into the status's two lines.
    for (size_t passed = 0; passed <= longest.checks.size(); ++passed) {
        if (passed > 0) {
            rv_editor_check_set(longest, passed - 1, rv_editor_check_state::passed, "ok", "env");
        }
        for (const auto building : { rv_editor_release_building::none,
                 rv_editor_release_building::candidate,
                 rv_editor_release_building::development }) {
            facts =
                std::max(facts, rv_editor_strip_status_width_for(rv_editor_release_facts_make(&longest, building).first_line));
        }
    }
    cached_font = font;
    cached_size = font->LegacySize;
    cached_width = facts;
    return cached_width;
}

float rv_editor_release_column_min_width()
{
    return rv_editor_button_width_for(
        { rv_editor_text("pane_candidate.force_stop_button"), rv_editor_text("pane_candidate.cancel_build_button") });
}

float rv_editor_release_min_width()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const float gap = style.ItemSpacing.x;
    const float buttons = static_cast<float>(release_buttons) * ImGui::GetFrameHeight() + (release_buttons - 1) * gap;
    // As Runtime Controls': the window's and the shelf's left padding, the squares, then the column to the tile's edge.
    const float row = style.WindowPadding.x + style.ItemInnerSpacing.x + buttons + gap + rv_editor_release_column_min_width();
    // The status under the squares starts after the left paddings and, as the slot, reaches the tile's edge at this width.
    return std::max(row, rv_editor_release_status_min_width() + style.WindowPadding.x + style.ItemInnerSpacing.x);
}

float rv_editor_release_height(float width)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const float side = rv_editor_transport_side(width, rv_editor_release_column_min_width(), release_buttons);
    // The squares' row (the action slot is no taller), then the status's two lines.
    return style.WindowPadding.y + 2.0f * style.ItemInnerSpacing.y + side + style.ItemSpacing.y +
        rv_editor_strip_status_height() + style.ItemSpacing.y;
}

} // namespace rv_editor
