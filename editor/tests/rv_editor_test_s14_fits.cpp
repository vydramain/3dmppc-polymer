// S14, AC1/AC15: Runtime Controls, Release Controls, the Toolchest and Game at the minimum of their kind show every element
// whole in every state of the plan's table; the strips keep the same rectangles in every state with a project; one pixel
// less in width (and, for the Toolchest, in height) breaks at least one state of a strip (Game's status is drawn text, not
// items: its width is held by the picture tests).

#include <cstdio>
#include <string>

#include "rv_editor_test.hpp"
#include "rv_editor_test_s14.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s14;

namespace
{

// "" or the first violation of any state of `all` at `min`.
std::string run_states(rv_editor_test_imgui &gui, states &all, stable_tiles::minimum min)
{
    // The no-project state is the Open Project row: its text has "..." (fits_whole takes that for an added ellipsis), so it
    // has a test of its own below.
    for (size_t i = all.project_from; i < all.shells.size(); ++i) {
        const std::string bad = rv_editor_check_pane_states(gui, *all.shells[i], all.kind, { [] {} }, min);
        if (!bad.empty()) {
            return "state '" + all.names[i] + "': " + bad;
        }
    }
    return "";
}

} // namespace

// The state without a project at the minimum: every item lies inside the tile (the row's "Open Project..." text would make
// fits_whole report an ellipsis that is only the verb's dots).
RV_EDITOR_TEST(rv_editor_test_s14_the_no_project_state_stays_inside_the_tile)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind :
        { rv_editor_pane_kind::controls, rv_editor_pane_kind::release_controls, rv_editor_pane_kind::game }) {
        states all(kind);
        const auto min = stable_tiles::pane_minimum(gui, kind);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        const auto r = draw_at(gui, *all.shells[0], kind, min.w, min.h);
        RV_EDITOR_CHECK(!r.entries.empty());
        for (const auto &e : r.entries) {
            if (e.x0 < 0 || e.y0 < 0 || e.x1 > min.w || e.y1 > min.h) {
                std::fprintf(stderr,
                    "  %s no project at %.0fx%.0f: %s is outside the tile\n",
                    name_of(kind),
                    min.w,
                    min.h,
                    rv_editor_probe_detail::rect(e).c_str());
            }
            RV_EDITOR_CHECK(e.x0 >= 0 && e.y0 >= 0 && e.x1 <= min.w && e.y1 <= min.h);
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_s14_every_state_fits_whole_at_the_minimum)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : strip_kinds()) {
        states all(kind);
        const auto min = stable_tiles::pane_minimum(gui, kind);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        const std::string bad = run_states(gui, all, min);
        if (!bad.empty()) {
            std::fprintf(stderr, "  %s at %.0fx%.0f: %s\n", name_of(kind), min.w, min.h, bad.c_str());
        }
        RV_EDITOR_CHECK(bad.empty());
    }
}

// AC1: the minimum is exact only if it passes and one pixel less, in width or in height, does not.
RV_EDITOR_TEST(rv_editor_test_s14_one_pixel_under_the_minimum_breaks_a_strip_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind :
        { rv_editor_pane_kind::controls, rv_editor_pane_kind::release_controls, rv_editor_pane_kind::toolchest }) {
        states all(kind);
        const auto min = stable_tiles::pane_minimum(gui, kind);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        RV_EDITOR_CHECK(run_states(gui, all, min).empty());
        const std::string narrow = run_states(gui, all, { min.w - 1.0f, min.h });
        if (narrow.empty()) {
            std::fprintf(stderr, "  %s: no violation at the minimum width less 1 px\n", name_of(kind));
        }
        RV_EDITOR_CHECK(!narrow.empty());
        // AC1: the transport's height is fixed by its strip, not by what its items need: only the Toolchest is tested for it.
        const std::string low = kind == rv_editor_pane_kind::toolchest ? run_states(gui, all, { min.w, min.h - 1.0f }) : "x";
        if (low.empty()) {
            std::fprintf(stderr, "  %s: no violation at the minimum height less 1 px\n", name_of(kind));
        }
        RV_EDITOR_CHECK(!low.empty());
    }
}

// AC15, R14: with a project, every state of Runtime Controls and of Release Controls puts the buttons, the action slot, the
// slot's and the message row's windows at the same rectangles, at the minimum width, at 640 px and wider.
RV_EDITOR_TEST(rv_editor_test_s14_transport_states_keep_every_rectangle)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : { rv_editor_pane_kind::controls, rv_editor_pane_kind::release_controls }) {
        states all(kind);
        const auto min = stable_tiles::pane_minimum(gui, kind);
        RV_EDITOR_CHECK(min.w > 0.0f);
        for (const float width : { min.w, 639.0f, 640.0f, 900.0f }) {
            if (width < min.w) {
                continue;
            }
            const auto base = draw_at(gui, *all.shells[all.project_from], kind, width, 400.0f);
            RV_EDITOR_CHECK(!base.entries.empty());
            for (size_t i = all.project_from + 1; i < all.shells.size(); ++i) {
                const std::string d = outer_difference(base, draw_at(gui, *all.shells[i], kind, width, 400.0f));
                if (!d.empty()) {
                    std::fprintf(stderr,
                        "  %s width %.0f, state '%s': %s\n",
                        name_of(kind),
                        width,
                        all.names[i].c_str(),
                        d.c_str());
                }
                RV_EDITOR_CHECK(d.empty());
            }
        }
    }
}

// The Toolchest's shelf has the same height in every state and never shows a scroll bar, at the minimum and wider.
RV_EDITOR_TEST(rv_editor_test_s14_toolchest_is_a_shelf_of_one_height_without_scroll_bars)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    states all(rv_editor_pane_kind::toolchest);
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::toolchest);
    RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
    for (const float width : { min.w, 500.0f, 900.0f }) {
        float first = -1.0f;
        for (size_t i = 0; i < all.shells.size(); ++i) {
            const auto r = draw_at(gui, *all.shells[i], rv_editor_pane_kind::toolchest, width, min.h);
            for (const auto &e : r.entries) {
                if (e.scrollbar) {
                    std::fprintf(stderr,
                        "  toolchest width %.0f, state '%s': %s shows a scroll bar\n",
                        width,
                        all.names[i].c_str(),
                        e.label.c_str());
                }
                RV_EDITOR_CHECK(!e.scrollbar);
            }
            if (first < 0.0f) {
                first = r.content_height;
            }
            if (r.content_height != first) {
                std::fprintf(stderr,
                    "  toolchest width %.0f, state '%s': height %.1f, first state %.1f\n",
                    width,
                    all.names[i].c_str(),
                    r.content_height,
                    first);
            }
            RV_EDITOR_CHECK(r.content_height == first);
            RV_EDITOR_CHECK(r.content_height <= min.h + 0.5f);
        }
    }
}
