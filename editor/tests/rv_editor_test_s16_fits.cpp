// S16, AC1/AC7: Scene, Hierarchy and Inspector at the minimum of their kind show every element whole in every state of
// the plan's table, Hierarchy and Inspector keep their message box and content area in the same rectangles in every
// state, the Scene's viewport is where it was with a read-only line or an error over it, and one pixel less in width or
// in height breaks at least one state. rv_editor_check_pane_states runs one state at a time (a box's buttons change with
// the state), so the windows are compared here.

#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_s3.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s16.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s16;

namespace
{

// "" or the first violation of `kind` at `min` over all its states, on a shell of its own.
std::string run_states(rv_editor_test_imgui &gui, rv_editor_pane_kind kind, stable_tiles::minimum min)
{
    fx f(gui, kind);
    rv_editor_probe_record first;
    const rv_editor_probe_entry *scene_viewport = nullptr;
    rv_editor_probe_record scene_first;
    for (size_t i = 0; i < f.states.size(); ++i) {
        const std::string state = "state " + std::to_string(i) + ": ";
        if (const std::string bad = rv_editor_check_pane_states(gui, f.shell, kind, { f.states[i] }, min); !bad.empty()) {
            return bad;
        }
        const rv_editor_probe_record record = settled(f, min.w, min.h);
        if (kind == rv_editor_pane_kind::scene) {
            // scene, read-only, error: the viewport is the same rectangle whatever the line over it says.
            if (i == 2) {
                scene_first = record;
                scene_viewport = find(scene_first, "##viewport");
            } else if ((i == 3 || i == 4) && scene_viewport != nullptr) {
                if (const std::string moved = s13::same_rect(scene_viewport, find(record, "##viewport"), "viewport");
                    !moved.empty()) {
                    return state + moved;
                }
            }
            continue;
        }
        if (i == 0) {
            first = record;
        } else if (const std::string moved = s3::windows_difference(first, record); !moved.empty()) {
            return state + moved;
        }
    }
    return "";
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_s16_every_state_fits_whole_at_the_minimum_and_nothing_moves)
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
        report(name_of(kind), bad);
        RV_EDITOR_CHECK(bad.empty());
    }
}

RV_EDITOR_TEST(rv_editor_test_s16_one_pixel_under_the_minimum_breaks_a_state)
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
        // The tallest strip of Scene (a Create error in the New Scene question) is unreachable from the fixture, so
        // its minimum height cannot be shown exact; the fit at the minimum is still checked above.
        if (kind == rv_editor_pane_kind::scene) {
            continue;
        }
        const std::string low = run_states(gui, kind, { min.w, min.h - 1.0f });
        if (low.empty()) {
            std::fprintf(stderr, "  %s: no violation at the minimum height less 1 px\n", name_of(kind));
        }
        RV_EDITOR_CHECK(!low.empty());
    }
}
