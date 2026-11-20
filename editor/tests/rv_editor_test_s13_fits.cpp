// S13, AC1/AC7: Output, Runtime Log, Build Log, Terminal, Problems and Search at the minimum of their kind show every
// element whole in every state of the plan's table, keep their scroll windows (message box, content area) in the same
// rectangles in every state, and one pixel less in width or in height breaks at least one state. The buttons of a
// message box change with the state, so rv_editor_check_pane_states (which matches every item by id) runs one state at
// a time and the windows are compared here.

#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_strip.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s13;

namespace
{

// "" or the first violation of `kind` at `min` over all its states, on a shell of its own.
std::string run_states(rv_editor_test_imgui &gui, rv_editor_pane_kind kind, stable_tiles::minimum min)
{
    fixture f(gui, kind);
    rv_editor_probe_record first;
    for (size_t i = 0; i < f.states.size(); ++i) {
        const std::string state = "state " + std::to_string(i) + ": ";
        if (const std::string bad = rv_editor_check_pane_states(gui, f.shell, kind, { f.states[i] }, min); !bad.empty()) {
            return state + bad;
        }
        const rv_editor_probe_record record = settled(f, min.w, min.h);
        if (i == 0) {
            first = record;
        } else if (const std::string moved = (kind == rv_editor_pane_kind::inspector_report ? windows_difference(first, record) : strip::same_top(first, record)); !moved.empty()) {
            return state + moved;
        }
    }
    return "";
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_s13_every_state_fits_whole_at_the_minimum_and_nothing_moves)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : kinds()) {
        const auto min = stable_tiles::pane_minimum(gui, kind);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        if (min.w <= 0.0f || min.h <= 0.0f) {
            std::fprintf(stderr, "  %s: no minimum of its own to draw in\n", name_of(kind));
            continue;
        }
        const std::string bad = run_states(gui, kind, min);
        if (!bad.empty()) {
            std::fprintf(stderr, "  %s at %.0fx%.0f: %s\n", name_of(kind), min.w, min.h, bad.c_str());
        }
        RV_EDITOR_CHECK(bad.empty());
    }
}

RV_EDITOR_TEST(rv_editor_test_s13_one_pixel_under_the_minimum_breaks_a_state)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_pane_kind kind : kinds()) {
        const auto min = stable_tiles::pane_minimum(gui, kind);
        RV_EDITOR_CHECK(min.w > 0.0f && min.h > 0.0f);
        if (min.w <= 0.0f || min.h <= 0.0f) {
            std::fprintf(stderr, "  %s: no minimum of its own to go under\n", name_of(kind));
            continue;
        }
        // The minimum is exact only if it passes and one pixel less does not.
        RV_EDITOR_CHECK(run_states(gui, kind, min).empty());
        const std::string narrow = run_states(gui, kind, { min.w - 1.0f, min.h });
        if (narrow.empty()) {
            std::fprintf(stderr, "  %s: no violation at the minimum width less 1 px\n", name_of(kind));
        }
        RV_EDITOR_CHECK(!narrow.empty());
        const std::string low = run_states(gui, kind, { min.w, min.h - 1.0f });
        if (low.empty()) {
            std::fprintf(stderr, "  %s: no violation at the minimum height less 1 px\n", name_of(kind));
        }
        RV_EDITOR_CHECK(!low.empty());
    }
}
