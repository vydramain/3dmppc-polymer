// S14, R1/R3, AC1: Runtime Controls, Release Controls, the Toolchest and Game each have one minimum that no state
// changes and no drag or window size goes under, and the body of their tile never scrolls (it is not a scroll area and
// holds no content past its edges).

#include <cstdio>
#include <string>

#include "rv_editor_test.hpp"
#include "rv_editor_test_s14.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s14;

namespace
{

// The minimum the shell would set for pane 1 of `shell` now, measured inside a frame as the shell does.
stable_tiles::minimum minimum_of(rv_editor_test_imgui &gui, rv_editor_shell &shell, rv_editor_pane_kind kind)
{
    stable_tiles::minimum result{ 0, 0 };
    gui.frame([&] {
        shell.ws.minimums.clear();
        rv_editor_shell_set_minimum(shell, strip_pane, kind, { 0, 0 });
        const auto it = shell.ws.minimums.find(strip_pane);
        if (it != shell.ws.minimums.end()) {
            result = { static_cast<float>(it->second.w), static_cast<float>(it->second.h) };
        }
    });
    return result;
}

} // namespace

// R1: one minimum per kind, the same in every state of the plan's table.
RV_EDITOR_TEST(rv_editor_test_s14_every_kind_has_one_minimum_whatever_the_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : strip_kinds()) {
        states all(kind);
        const auto first = minimum_of(gui, *all.shells[0], kind);
        if (first.w <= 0.0f || first.h <= 0.0f) {
            std::fprintf(stderr, "  %s: no minimum of its own (%.0f x %.0f)\n", name_of(kind), first.w, first.h);
        }
        RV_EDITOR_CHECK(first.w > 0.0f && first.h > 0.0f);
        for (size_t i = 0; i < all.shells.size(); ++i) {
            const auto now = minimum_of(gui, *all.shells[i], kind);
            if (now.w != first.w || now.h != first.h) {
                std::fprintf(stderr,
                    "  %s state '%s': minimum %.0f x %.0f became %.0f x %.0f\n",
                    name_of(kind),
                    all.names[i].c_str(),
                    first.w,
                    first.h,
                    now.w,
                    now.h);
            }
            RV_EDITOR_CHECK(now.w == first.w && now.h == first.h);
        }
    }
}

// R1: a splitter dragged to either end, or a window smaller than the tree, leaves each tile at least its minimum.
RV_EDITOR_TEST(rv_editor_test_s14_a_drag_or_a_small_window_keeps_each_tile_at_its_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : strip_kinds()) {
        const auto min = stable_tiles::pane_minimum(gui, kind);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        for (const bool strip_first : { true, false }) {
            for (const rv_editor_tile_dock dock : { rv_editor_tile_dock::right, rv_editor_tile_dock::bottom }) {
                for (const float ratio : { 0.02f, 0.98f }) {
                    for (const ImVec2 window : { ImVec2(300, 200), ImVec2(1600, 900) }) {
                        own_shell own;
                        tiles t(gui, own.shell, kind, strip_first, dock, false);
                        RV_EDITOR_CHECK(rv_editor_tile_set_ratio(own.shell.ws.layout, t.split(), ratio) == RV_OK);
                        t.settle(window.x, window.y, 3);
                        if (t.s.size.x < min.w || t.s.size.y < min.h) {
                            std::fprintf(stderr,
                                "  %s ratio %.2f window %.0fx%.0f: tile body %.0fx%.0f under the minimum %.0fx%.0f\n",
                                name_of(kind),
                                ratio,
                                window.x,
                                window.y,
                                t.s.size.x,
                                t.s.size.y,
                                min.w,
                                min.h);
                        }
                        RV_EDITOR_CHECK(t.s.size.x >= min.w && t.s.size.y >= min.h);
                    }
                }
            }
        }
    }
}

// R3, AC1, AC21: the body of the tile is drawn at the tile's minimum in every state: it is not a scroll window and has no
// content past its edges.
RV_EDITOR_TEST(rv_editor_test_s14_the_tile_body_never_scrolls_at_the_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : strip_kinds()) {
        states all(kind);
        for (size_t i = 0; i < all.shells.size(); ++i) {
            tiles t(gui, *all.shells[i], kind, true, rv_editor_tile_dock::bottom, false);
            // A window smaller than any minimum: the tile takes its minimum, and the body is what is left of it.
            t.settle(20, 20, 4);
            if (body_is_scroll_area(t.record)) {
                std::fprintf(stderr,
                    "  %s state '%s': the tile body is a scroll window\n",
                    name_of(kind),
                    all.names[i].c_str());
            }
            RV_EDITOR_CHECK(!body_is_scroll_area(t.record));
            if (t.s.overflow.x > 0.5f || t.s.overflow.y > 0.5f) {
                std::fprintf(stderr,
                    "  %s state '%s': content past the body by %.1f x %.1f px (body %.0fx%.0f)\n",
                    name_of(kind),
                    all.names[i].c_str(),
                    t.s.overflow.x,
                    t.s.overflow.y,
                    t.s.size.x,
                    t.s.size.y);
            }
            RV_EDITOR_CHECK(t.s.overflow.x <= 0.5f && t.s.overflow.y <= 0.5f);
        }
    }
}
