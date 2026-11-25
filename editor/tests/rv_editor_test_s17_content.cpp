// S17, R1/R4/R11/R19/R20, AC21: what every converted pane puts where, in every state of the plan's table. A pane has no
// message box (what it says is a strip under its areas, drawn only with a text), its content areas start at the same
// place whatever the state, every area keeps its floor height, and no stretch of the tile is left empty.

#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s17_states.hpp"
#include "ui/rv_editor_widgets_content.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s17;

namespace
{

// The kinds that say something to the user in a message box (Checks is a table alone, Session has Notes in place of a
// block, the empty pane has no state).
const rv_editor_pane_kind boxed[] = { rv_editor_pane_kind::export_,
    rv_editor_pane_kind::candidate,
    rv_editor_pane_kind::observe,
    rv_editor_pane_kind::build_result };

void report(const char *kind, const char *state, const std::string &bad)
{
    if (!bad.empty()) {
        std::fprintf(stderr, "  %s, %s: %s\n", kind, state, bad.c_str());
    }
}

} // namespace

// R1, R11, R4: no state of a pane has a message box (Export's description is its one text window) or an ask place, and has a
// content area that is at least three rows and a bar high; the areas start at the same place from state to state.
RV_EDITOR_TEST(rv_editor_test_s17_no_state_has_a_message_box_and_every_area_stays_put)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    float rows_h = 0.0f;
    gui.frame([&] {
        rows_h = rv_editor_content_min_height();
    });
    for (const rv_editor_pane_kind kind : boxed) {
        fixture f(gui, kind);
        const auto min = tile_of(gui, kind);
        layouts seen;
        for (size_t i = 0; i < f.states.size(); ++i) {
            f.states[i].set();
            const rv_editor_probe_record r = settled(f, min.w, min.h);
            const state &st = f.states[i];
            const size_t allowed = kind == rv_editor_pane_kind::export_ ? 1 : 0;
            RV_EDITOR_CHECK(texts(r).size() <= allowed);
            RV_EDITOR_CHECK(!s13::has_ask_place(r));
            if (texts(r).size() > allowed) {
                report(name_of(kind), st.name, "a message box is drawn");
            }
            if (st.project) {
                RV_EDITOR_CHECK(!areas(r).empty());
                if (areas(r).empty()) {
                    report(name_of(kind), st.name, "no content area");
                }
                for (const auto *area : areas(r)) {
                    if (area->y1 - area->y0 < rows_h) {
                        report(name_of(kind), st.name, "an area is under three rows and a bar high");
                    }
                    RV_EDITOR_CHECK(area->y1 - area->y0 >= rows_h);
                }
            }
            const std::string moved = seen.add(st, r);
            report(name_of(kind), st.name, moved);
            RV_EDITOR_CHECK(moved.empty());
        }
    }
}

// R20, AC21: at its minimum no state of any converted pane leaves a stretch of the tile that nothing is drawn in (the
// empty block above a tree, a list, a table or a box), and nothing is left under the last block either.
RV_EDITOR_TEST(rv_editor_test_s17_no_state_leaves_an_empty_block)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    const float limit = block_gap_limit(gui);
    for (const rv_editor_pane_kind kind : kinds()) {
        fixture f(gui, kind);
        const auto min = tile_of(gui, kind);
        for (size_t i = 0; i < f.states.size(); ++i) {
            f.states[i].set();
            const std::string bad = empty_block(settled(f, min.w, min.h), min.h, limit);
            report(name_of(kind), f.states[i].name, bad);
            RV_EDITOR_CHECK(bad.empty());
        }
    }
}

// R11, AC12, R1: the one text window left (Export's description of the kind) is exactly two lines of text and the padding
// high in every state, and no other kind has one.
RV_EDITOR_TEST(rv_editor_test_s17_the_description_is_two_lines_high_and_no_message_box_is_left)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    float line = 0.0f;
    float pad = 0.0f;
    gui.frame([&] {
        line = ImGui::GetTextLineHeight();
        pad = ImGui::GetStyle().WindowPadding.y;
    });
    for (const rv_editor_pane_kind kind : boxed) {
        fixture f(gui, kind);
        for (size_t i = 0; i < f.states.size(); ++i) {
            const auto r = at_minimum(gui, kind, i);
            const auto t = texts(r);
            RV_EDITOR_CHECK(t.size() <= (kind == rv_editor_pane_kind::export_ ? 1u : 0u));
            for (const auto *box : t) {
                const float h = box->y1 - box->y0;
                const float want = 2.0f * line + 2.0f * pad;
                if (h < want - 0.6f || h > want + 0.6f) {
                    std::fprintf(stderr,
                        "  %s, %s: a text window is %.1f high, two lines and the padding are %.1f\n",
                        name_of(kind),
                        f.states[i].name,
                        h,
                        want);
                }
                RV_EDITOR_CHECK(h >= want - 0.6f && h <= want + 0.6f);
            }
        }
    }
}
