// Release Controls: the first line beside the tiles, the sha256 hover and the tile size choice.

#include "panes/rv_editor_release_facts.hpp"

#include <algorithm>
#include <iterator>

#include "imgui.h"

#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Tile sizes tried, largest first: the first whose row fits wins, as in the transport bar.
constexpr rv_editor_tile_size release_sizes[] = {
    rv_editor_tile_size::large,
    rv_editor_tile_size::medium,
    rv_editor_tile_size::small,
};

} // namespace

rv_editor_release_facts rv_editor_release_facts_make(const rv_editor_candidate *c, rv_editor_release_building building)
{
    rv_editor_release_facts facts;
    if (c == nullptr) {
        facts.first_line = rv_editor_text("pane_candidate.no_candidate_yet_facts");
        facts.second_line = false;
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

// The text whose width the facts' room is, whatever the state says.
constexpr const char *facts_room_text = "MMMMMMMMMMMMMMMMMMMM";

rv_editor_tile_size rv_editor_release_tile_size(const rv_editor_tile (&tiles)[5], float facts_width, float avail)
{
    for (const rv_editor_tile_size size : release_sizes) {
        float width = facts_width + ImGui::GetStyle().ItemSpacing.x * static_cast<float>(std::size(tiles));
        for (const rv_editor_tile &tile : tiles) {
            width += rv_editor_tile_width(tile, size);
        }
        if (width <= avail) {
            return size;
        }
    }
    return rv_editor_tile_size::small;
}

rv_editor_tile_size rv_editor_release_tile_size(const rv_editor_tile (&tiles)[5], float avail)
{
    // The facts keep a constant room: the width of a facts line of twenty "M" is room enough to read it.
    return rv_editor_release_tile_size(tiles, ImGui::CalcTextSize(facts_room_text).x, avail);
}

} // namespace rv_editor
