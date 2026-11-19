// S19, AC1/AC7: Recent Projects, New Project, Open Project, Settings, Help and the manual at the minimum of their view
// show every element whole in every state of the plan's table, no item over another (a label over its field), keep their
// items and content areas in the same rectangles in every state, and one pixel less in width or in height breaks at
// least one state.

#include <cstdio>
#include <string>

#include "rv_editor_test.hpp"
#include "rv_editor_test_s19_check.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s19;

namespace
{

// "" or the first violation of `v` at `min`, on a shell of its own.
std::string violation(rv_editor_test_imgui &gui, view v, minimum min)
{
    fixture f(gui, v);
    return run_states(f, min);
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_s19_every_state_fits_whole_at_the_minimum_and_nothing_moves)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const view v : views()) {
        const minimum min = minimum_of(gui, v);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        if (min.w <= 0.0f || min.h <= 0.0f) {
            std::fprintf(stderr, "  %s: no minimum of its own to draw in\n", name_of(v));
            continue;
        }
        const std::string bad = violation(gui, v, min);
        if (!bad.empty()) {
            std::fprintf(stderr, "  %s at %.0fx%.0f: %s\n", name_of(v), min.w, min.h, bad.c_str());
        }
        RV_EDITOR_CHECK(bad.empty());
    }
}

RV_EDITOR_TEST(rv_editor_test_s19_one_pixel_under_the_minimum_breaks_a_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const view v : views()) {
        const minimum min = minimum_of(gui, v);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        if (min.w <= 0.0f || min.h <= 0.0f) {
            std::fprintf(stderr, "  %s: no minimum of its own to go under\n", name_of(v));
            continue;
        }
        // The minimum is exact only if it passes and one pixel less does not.
        RV_EDITOR_CHECK(violation(gui, v, min).empty());
        const std::string narrow = violation(gui, v, { min.w - 1.0f, min.h });
        if (narrow.empty()) {
            std::fprintf(stderr, "  %s: no violation at the minimum width less 1 px\n", name_of(v));
        }
        RV_EDITOR_CHECK(!narrow.empty());
        const std::string low = violation(gui, v, { min.w, min.h - 1.0f });
        if (low.empty()) {
            std::fprintf(stderr, "  %s: no violation at the minimum height less 1 px\n", name_of(v));
        }
        RV_EDITOR_CHECK(!low.empty());
    }
}
