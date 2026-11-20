// S13, R4/R19/R20, AC5: what each converted pane puts where. The logs keep their lines in a content area under a shelf
// and a header, with no message block (a message is the strip under the area, see the strip tests); Problems keeps a build
// failure in the message box (no ask place), the summary over the table at one place, the table in the content area and its
// level column pinned; Search has its shelf and its table, Terminal its screen at the top: neither reserves a message block.

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

constexpr size_t pinned_band_lines = 6;

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

// "" when the pane has a message box above a content area and no ask place; else what is wrong.
std::string has_box_over_area(const rv_editor_probe_record &r)
{
    const auto *box = message_box(r);
    const auto *area = content_area(r);
    if (box == nullptr) {
        return "no message box";
    }
    if (area == nullptr) {
        return "no content area";
    }
    if (has_ask_place(r)) {
        return "an ask place is still drawn";
    }
    return box->y1 <= area->y0 ? "" : "the message box is not above the content area";
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

// R20: a build failure is in the message box with no ask place, and every state keeps the summary over the table at the
// same place and the table in the content area.
RV_EDITOR_TEST(rv_editor_test_s13_problems_say_a_failure_in_the_message_box_and_keep_the_table_in_the_area)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::inspector_report);
    // 0 no project, 1 empty, 2 build failed, 3 problems, 4 building.
    std::vector<shot> r;
    for (size_t i = 0; i < f.states.size(); ++i) {
        r.push_back(at_minimum(gui, f, i));
        RV_EDITOR_CHECK(!has_ask_place(r.back()));
        if (i >= 1) {
            const std::string bad = has_box_over_area(r.back());
            report("problems", bad);
            RV_EDITOR_CHECK(bad.empty());
        }
    }
    RV_EDITOR_CHECK(r.size() == 5);
    if (r.size() != 5) {
        return;
    }
    // The failure's text is the box's: its text window draws more than it does without a failure.
    RV_EDITOR_CHECK(r[2].box_drawn > r[1].box_drawn);
    // The area is where it was whatever the state says, and the problems are rows in it.
    for (size_t i = 1; i < r.size(); ++i) {
        const std::string moved = same_rect(content_area(r[1]), content_area(r[i]), "content area");
        report("problems", moved);
        RV_EDITOR_CHECK(moved.empty());
    }
    RV_EDITOR_CHECK(shows_row_in(r[3], content_area(r[3])));
    RV_EDITOR_CHECK(shows_row_in(r[4], content_area(r[4])));
    // The summary (the last text above the area, below the box) is one place with problems, idle or building.
    float box_h = 0.0f;
    gui.frame([&] {
        box_h = rv_editor_message_box_height();
    });
    const auto summary = [box_h](const rv_editor_probe_record &rec) -> const rv_editor_probe_entry * {
        const auto *area = content_area(rec);
        const auto *box = message_box(rec);
        const rv_editor_probe_entry *found = nullptr;
        for (const auto &e : rec.entries) {
            const bool text = e.label.empty() && !e.scroll_area;
            const bool in_box = box != nullptr && e.y0 >= box->y0 && e.y1 <= box->y0 + box_h;
            if (text && area != nullptr && !in_box && e.y1 <= area->y0 + 0.5f && (found == nullptr || e.y1 >= found->y1)) {
                found = &e;
            }
        }
        return found;
    };
    const auto *idle = summary(r[3]);
    const auto *busy = summary(r[4]);
    RV_EDITOR_CHECK(idle != nullptr && busy != nullptr);
    if (idle != nullptr && busy != nullptr) {
        RV_EDITOR_CHECK(idle->x0 == busy->x0 && idle->y0 == busy->y0 && idle->y1 == busy->y1);
    }
}

// R4, R10: the level column of the Problems table stays at the left edge of the area while the table scrolls sideways.
RV_EDITOR_TEST(rv_editor_test_s13_problems_level_column_is_pinned_to_the_left_edge)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    fixture f(gui, rv_editor_pane_kind::inspector_report);
    const auto min = stable_tiles::pane_minimum(gui, f.kind);
    RV_EDITOR_CHECK(min.w > 0.0f);
    const rv_editor_probe_record before = at_minimum(gui, f, 3);
    const auto *area = content_area(before);
    RV_EDITOR_CHECK(area != nullptr);
    if (area == nullptr) {
        return;
    }
    const rv_editor_probe_entry *inner = nullptr;
    for (const auto &e : before.entries) {
        inner = is_area_scroll(e) ? &e : inner;
    }
    ImGuiWindow *w = inner != nullptr ? ImGui::FindWindowByName(inner->label.c_str()) : nullptr;
    RV_EDITOR_CHECK(w != nullptr && w->ScrollMax.x > 0.0f); // the long message and path need the horizontal bar
    if (w == nullptr) {
        return;
    }
    w->Scroll.x = w->ScrollMax.x;
    const auto after = settled(f, min.w, min.h);
    float line = 0.0f;
    gui.frame([&] {
        line = ImGui::GetTextLineHeight();
    });
    int pinned = 0;
    for (const auto &e : after.entries) {
        if (e.label.find('/') != std::string::npos || e.label == "##track" || e.x1 - e.x0 < 1.0f || !inside(e, *area)) {
            continue;
        }
        if (e.x0 >= area->x0 && e.x0 < area->x0 + pinned_band_lines * line) {
            for (const auto &b : before.entries) {
                pinned += b.x0 == e.x0 && b.y0 == e.y0 && b.x1 == e.x1 && b.y1 == e.y1 ? 1 : 0;
            }
        }
    }
    if (pinned == 0) {
        std::fprintf(stderr, "  problems: nothing stays at the left edge after scrolling the table sideways\n");
    }
    RV_EDITOR_CHECK(pinned > 0);
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
                std::fprintf(stderr, "  terminal state %zu: the screen starts %.0f px down, the tile's padding is %.0f\n", i, area->y0, pad);
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
