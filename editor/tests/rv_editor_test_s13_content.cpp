// S13, R4/R19/R20, AC5: what each converted pane puts where. The logs keep their lines in a content area under a shelf
// S13, R4/R19/R20, AC5: what each converted pane puts where. The logs keep their lines in a content area under a shelf
// and a header, with no message block (a message is the strip under the area, see the strip tests); the Inspector Report
// keeps the table in the content area at the pane's top and a build failure in a strip under it (no box, no ask place);
// Search has its shelf and its table, Terminal its screen at the top: neither reserves a message block.

#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s13.hpp"
#include "rv_editor_test_strip.hpp"
#include "ui/rv_editor_widgets_message.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using namespace rv_editor_test::s13;

namespace
{

// What the box's text window drew in the frame just ended: the draw commands of the window and its children.
int drawn_indices(const rv_editor_probe_entry *box)
{
    const ImGuiWindow *w = box != nullptr ? ImGui::FindWindowByName(box->label.c_str()) : nullptr;
    int n = 0;
    for (const ImGuiWindow *x : ImGui::GetCurrentContext()->Windows) {
        for (const ImGuiWindow *p = x; w != nullptr && p != nullptr; p = p->ParentWindow) {
            if (p == w) {
                for (const ImDrawCmd &cmd : x->DrawList->CmdBuffer) {
                    n += static_cast<int>(cmd.ElemCount);
                }
                break;
            }
        }
    }
    return n;
}

// One state drawn: the record and what the message box drew (so it can be compared with another state's).
struct shot : rv_editor_probe_record {
    int box_drawn = 0;
};

shot snap(fixture &f, float width, float height)
{
    shot s;
    static_cast<rv_editor_probe_record &>(s) = settled(f, width, height);
    s.box_drawn = drawn_indices(message_box(s));
    return s;
}

// A state of `f`, drawn at the kind's minimum.
shot at_minimum(rv_editor_test_imgui &gui, fixture &f, size_t state)
{
    const auto min = stable_tiles::pane_minimum(gui, f.kind);
    f.states[state]();
    return snap(f, min.w, min.h);
}

bool inside(const rv_editor_probe_entry &e, const rv_editor_probe_entry &area)
{
    const float cx = (e.x0 + e.x1) / 2.0f;
    const float cy = (e.y0 + e.y1) / 2.0f;
    return cx >= area.x0 && cx <= area.x1 && cy >= area.y0 && cy <= area.y1;
}

bool shows_row_in(const rv_editor_probe_record &r, const rv_editor_probe_entry *area)
{
    for (const auto &e : r.entries) {
        const bool row = e.label.size() >= 6 && e.label.compare(e.label.size() - 6, 6, "###row") == 0;
        if (row && area != nullptr && inside(e, *area)) {
            return true;
        }
    }
    return false;
}

void report(const char *what, const std::string &bad)
{
    if (!bad.empty()) {
        std::fprintf(stderr, "  %s: %s\n", what, bad.c_str());
    }
}

} // namespace

// AC5, R1: a log at its minimum shows three rows and the horizontal bar in the content area, under the shelf and the
// header, with no message block in any state; the states without a message draw the area at one rectangle.
RV_EDITOR_TEST(rv_editor_test_s13_logs_keep_their_lines_in_a_content_area_under_the_shelf)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    float rows_h = 0.0f;
    gui.frame([&] {
        rows_h = 3.0f * ImGui::GetTextLineHeight() + ImGui::GetStyle().ScrollbarSize;
    });
    for (const auto kind : { rv_editor_pane_kind::output, rv_editor_pane_kind::runtime_log, rv_editor_pane_kind::build_log }) {
        fixture f(gui, kind);
        rv_editor_probe_record first;
        for (size_t i = 0; i < f.states.size(); ++i) {
            const auto r = at_minimum(gui, f, i);
            RV_EDITOR_CHECK(!strip::has_message_window(r));
            const auto *area = content_area(r);
            RV_EDITOR_CHECK(area != nullptr);
            if (area != nullptr) {
                if (area->y1 - area->y0 < rows_h) {
                    std::fprintf(stderr,
                        "  %s state %zu: the area is %.1f high, three rows and a bar need %.1f\n",
                        name_of(kind),
                        i,
                        area->y1 - area->y0,
                        rows_h);
                }
                RV_EDITOR_CHECK(area->y1 - area->y0 >= rows_h);
            }
            // States 0 and 1 (an open project, lines) say nothing: the same windows.
            if (i == 0) {
                first = r;
            } else if (i == 1) {
                report(name_of(kind), windows_difference(first, r));
                RV_EDITOR_CHECK(windows_difference(first, r).empty());
            }
        }
    }
}

// R5, R20: the Inspector Report has no message box, no ask place and no summary line over the table: every state has the
// table's content area at the pane's top, and a failure is a strip under it that moves nothing above it.
RV_EDITOR_TEST(rv_editor_test_s13_problems_say_a_failure_in_a_strip_and_keep_the_table_in_the_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::inspector_report);
    // 0 no project, 1 empty, 2 build failed, 3 problems, 4 building.
    std::vector<shot> r;
    for (size_t i = 0; i < f.states.size(); ++i) {
        r.push_back(at_minimum(gui, f, i));
        RV_EDITOR_CHECK(!has_ask_place(r.back()));
        RV_EDITOR_CHECK(message_box(r.back()) == nullptr);
        RV_EDITOR_CHECK(content_area(r.back()) != nullptr);
    }
    RV_EDITOR_CHECK(r.size() == 5);
    if (r.size() != 5) {
        return;
    }
    // The area starts where it started whatever the state says; only its bottom gives way to a strip.
    for (size_t i = 1; i < r.size(); ++i) {
        const auto *first = content_area(r[1]);
        const auto *area = content_area(r[i]);
        if (first == nullptr || area == nullptr) {
            continue;
        }
        const bool same_top = first->x0 == area->x0 && first->y0 == area->y0 && first->x1 == area->x1;
        report("problems", same_top ? "" : "the content area moved");
        RV_EDITOR_CHECK(same_top);
    }
    RV_EDITOR_CHECK(shows_row_in(r[3], content_area(r[3])));
    RV_EDITOR_CHECK(shows_row_in(r[4], content_area(r[4])));
}

// Search: the shelf with the query and the hits in the content area, in every state with a project: no message block
// between them, the area at the same top whatever the state says (what it says is the strip under it).
RV_EDITOR_TEST(rv_editor_test_s13_search_has_a_shelf_and_a_table_in_the_area_with_no_block)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::search);
    // 0 no project, 1 not searched, 2 no matches, 3 hits, 4 truncated.
    std::vector<shot> r;
    for (size_t i = 0; i < f.states.size(); ++i) {
        r.push_back(at_minimum(gui, f, i));
        RV_EDITOR_CHECK(!strip::has_message_window(r.back()));
        if (i == 0) {
            continue;
        }
        RV_EDITOR_CHECK(content_area(r.back()) != nullptr);
        const rv_editor_probe_entry *query = nullptr;
        for (const auto &e : r.back().entries) {
            query = query == nullptr && e.label.compare(0, 7, "##query") == 0 ? &e : query;
        }
        RV_EDITOR_CHECK(query != nullptr && content_area(r.back()) != nullptr && query->y1 <= content_area(r.back())->y0);
    }
    RV_EDITOR_CHECK(r.size() == 5);
    if (r.size() != 5) {
        return;
    }
    RV_EDITOR_CHECK(shows_row_in(r[3], content_area(r[3])));
    RV_EDITOR_CHECK(shows_row_in(r[4], content_area(r[4])));
    for (size_t i = 2; i < r.size(); ++i) {
        const auto *a = content_area(r[1]);
        const auto *b = content_area(r[i]);
        RV_EDITOR_CHECK(a != nullptr && b != nullptr);
        if (a != nullptr && b != nullptr) {
            RV_EDITOR_CHECK(a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1);
        }
    }
}

// R19, R20: the Terminal reserves no room for its questions or a message: its screen is at the top of the tile in every
// state, with no window of the old box, and starts at one place whatever the state says.
RV_EDITOR_TEST(rv_editor_test_s13_terminal_has_no_reserve_and_its_screen_stays_at_the_top)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::terminal);
    float pad = 0.0f;
    float gap = 0.0f;
    gui.frame([&] {
        pad = ImGui::GetStyle().WindowPadding.y;
        gap = ImGui::GetStyle().ItemSpacing.y;
    });
    // 0 running, 1 not started, 2 ended, 3 paste question, 4 end question.
    std::vector<shot> r;
    constexpr float tall = 700.0f;
    for (size_t i = 0; i < f.states.size(); ++i) {
        f.states[i]();
        r.push_back(snap(f, 600.0f, tall));
        RV_EDITOR_CHECK(!strip::has_message_window(r.back()));
        const auto *area = content_area(r.back());
        RV_EDITOR_CHECK(area != nullptr);
        if (area != nullptr) {
            if (area->y0 > pad + gap + 0.5f) {
                std::fprintf(stderr,
                    "  terminal state %zu: the screen starts %.0f px down, the tile's padding is %.0f\n",
                    i,
                    area->y0,
                    pad);
            }
            RV_EDITOR_CHECK(area->y0 <= pad + gap + 0.5f);
        }
    }
    RV_EDITOR_CHECK(r.size() == 5);
    if (r.size() != 5) {
        return;
    }
    for (size_t i = 1; i < r.size(); ++i) {
        const auto *a = content_area(r[0]);
        const auto *b = content_area(r[i]);
        RV_EDITOR_CHECK(a != nullptr && b != nullptr);
        if (a != nullptr && b != nullptr) {
            RV_EDITOR_CHECK(a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1);
        }
    }
}
