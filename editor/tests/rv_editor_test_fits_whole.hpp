// fits_whole: every element of a pane lies inside its tile and its text is not wider or higher than its rectangle.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_stable_tiles.hpp"

namespace rv_editor_test
{

// "" or the first violation: an item outside `tile`, a text wider or higher than its rectangle, an added "..." in the
// label, a visible scroll bar on a window that is not a scroll area. Items inside scroll areas are not checked.
inline std::string rv_editor_fits_whole(const rv_editor_probe_record &record, const ImRect &tile)
{
    using namespace rv_editor_probe_detail;
    for (const auto &e : record.entries) {
        if (e.in_scroll) {
            continue;
        }
        if (e.x0 < tile.Min.x || e.y0 < tile.Min.y || e.x1 > tile.Max.x || e.y1 > tile.Max.y) {
            return name(e) + " " + rect(e) + " is outside the tile " + rect(tile.Min.x, tile.Min.y, tile.Max.x, tile.Max.y);
        }
        if (e.text_w > e.x1 - e.x0 || e.text_h > e.y1 - e.y0) {
            return name(e) + " " + rect(e) + " is smaller than its text " + std::to_string(e.text_w) + "x" +
                std::to_string(e.text_h);
        }
        const std::string visible = e.label.substr(0, e.label.find("##"));
        if (visible.find("...") != std::string::npos) {
            return name(e) + " shows an added ellipsis";
        }
        if (e.scrollbar && !e.scroll_area) {
            return name(e) + " shows a scroll bar but is not a scroll area";
        }
    }
    return "";
}

// Draws `kind` of `shell` in a tile of `min` once per state (each state changes the shell), checks fits_whole and that
// all states give the same rectangles, and returns "" or the first violation.
inline std::string rv_editor_check_pane_states(rv_editor_test_imgui &gui,
    rv_editor::rv_editor_shell &shell,
    rv_editor::rv_editor_pane_kind kind,
    const std::vector<std::function<void()>> &states,
    stable_tiles::minimum min)
{
    const ImRect tile(0, 0, min.w, min.h);
    auto draw = [&] {
        return rv_editor_probe_draw(
            gui,
            min.w,
            [&](rv_editor_probe_record &) {
                rv_editor::rv_editor_shell_pane(&shell, 1, kind, stable_tiles::theme);
            },
            min.h);
    };
    rv_editor_probe_record first;
    for (size_t i = 0; i < states.size(); ++i) {
        states[i]();
        draw(); // a settling frame: what depends on the previous frame has to catch up
        const rv_editor_probe_record record = draw();
        const std::string state = "state " + std::to_string(i) + ": ";
        if (const std::string bad = rv_editor_fits_whole(record, tile); !bad.empty()) {
            return state + bad;
        }
        if (i == 0) {
            first = record;
        } else if (const std::string moved = rv_editor_probe_compare(first, record); !moved.empty()) {
            return state + moved;
        }
    }
    return "";
}

} // namespace rv_editor_test
