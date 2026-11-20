// A widget that looks like a button, card, tile or row answers a click on its whole area: the four corners (2 px in)
// and the centre. A selectable table row: one click selects, double click or Enter acts.

#include <functional>

#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_table.hpp"
#include "ui/rv_editor_widgets_tile.hpp"

using namespace rv_editor;

namespace
{

constexpr float inset = 2.0f;

// One frame; `draw` returns what the widget reported and sets `rect` to its item rectangle.
using drawer = std::function<bool(ImRect &rect)>;

bool run_frame(rv_editor_test::rv_editor_test_imgui &gui, const drawer &draw, ImRect &rect)
{
    bool r = false;
    gui.frame([&]() {
        r = draw(rect);
    });
    return r;
}

// Moves the pointer to `p`, presses and releases over three frames; true when any of them reported.
bool click_at(rv_editor_test::rv_editor_test_imgui &gui, const drawer &draw, ImVec2 p)
{
    ImRect rect;
    ImGuiIO &io = ImGui::GetIO();
    bool hit = false;
    io.AddMousePosEvent(p.x, p.y);
    hit |= run_frame(gui, draw, rect);
    io.AddMouseButtonEvent(0, true);
    hit |= run_frame(gui, draw, rect);
    io.AddMouseButtonEvent(0, false);
    hit |= run_frame(gui, draw, rect);
    hit |= run_frame(gui, draw, rect);
    io.AddMousePosEvent(-1000.0f, -1000.0f);
    run_frame(gui, draw, rect);
    return hit;
}

// Clicks the four corners (inset) and the centre of what `draw` shows; returns how many of the five reported.
int corner_hits(const drawer &draw)
{
    rv_editor_test::rv_editor_test_imgui gui;
    ImRect rect;
    for (int i = 0; i < 3; ++i) {
        run_frame(gui, draw, rect);
    }
    const ImVec2 pts[5] = { { rect.Min.x + inset, rect.Min.y + inset },
        { rect.Max.x - inset, rect.Min.y + inset },
        { rect.Min.x + inset, rect.Max.y - inset },
        { rect.Max.x - inset, rect.Max.y - inset },
        { (rect.Min.x + rect.Max.x) / 2, (rect.Min.y + rect.Max.y) / 2 } };
    int hits = 0;
    for (const ImVec2 &p : pts) {
        hits += click_at(gui, draw, p) ? 1 : 0;
    }
    return hits;
}

bool last_rect(ImRect &rect)
{
    rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    return true;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_click_area_card)
{
    const rv_editor_theme theme = rv_editor_theme_olive;
    RV_EDITOR_CHECK(corner_hits([&](ImRect &r) {
        const bool c = rv_editor_card("card", ImVec2(180, 60), theme);
        last_rect(r);
        return c;
    }) == 5);
}

RV_EDITOR_TEST(rv_editor_test_click_area_card_selected)
{
    const rv_editor_theme theme = rv_editor_theme_olive;
    RV_EDITOR_CHECK(corner_hits([&](ImRect &r) {
        const bool c = rv_editor_card("card", ImVec2(180, 60), theme, true);
        last_rect(r);
        return c;
    }) == 5);
}

RV_EDITOR_TEST(rv_editor_test_click_area_card_disabled_ignores_click)
{
    const rv_editor_theme theme = rv_editor_theme_olive;
    rv_editor_state off;
    off.disabled = "why";
    RV_EDITOR_CHECK(corner_hits([&](ImRect &r) {
        const bool c = rv_editor_card("card", ImVec2(180, 60), theme, false, off);
        last_rect(r);
        return c;
    }) == 0);
}

// Guards: widgets that already answer on their whole area keep doing so.
RV_EDITOR_TEST(rv_editor_test_click_area_button)
{
    const rv_editor_theme theme = rv_editor_theme_olive;
    RV_EDITOR_CHECK(corner_hits([&](ImRect &r) {
        const bool c = rv_editor_button("Burn", theme);
        last_rect(r);
        return c;
    }) == 5);
}

RV_EDITOR_TEST(rv_editor_test_click_area_tile_buttons)
{
    const rv_editor_theme theme = rv_editor_theme_olive;
    const rv_editor_tile tile = { "Play", "P", 0xffffff };
    for (const rv_editor_tile_size size :
        { rv_editor_tile_size::large, rv_editor_tile_size::medium, rv_editor_tile_size::small }) {
        RV_EDITOR_CHECK(corner_hits([&](ImRect &r) {
            const bool c = rv_editor_tile_button(tile, size, theme);
            last_rect(r);
            return c;
        }) == 5);
    }
}

RV_EDITOR_TEST(rv_editor_test_click_area_radio_and_checkbox)
{
    const rv_editor_theme theme = rv_editor_theme_olive;
    RV_EDITOR_CHECK(corner_hits([&](ImRect &r) {
        const bool c = rv_editor_radio("Release", false, theme);
        last_rect(r);
        return c;
    }) == 5);
    bool on = false;
    RV_EDITOR_CHECK(corner_hits([&](ImRect &r) {
        const bool c = rv_editor_checkbox("Web tools", &on, theme);
        last_rect(r);
        return c;
    }) == 5);
}

namespace
{

// A table of two rows by three columns; `selected` is the picked row (-1: none). Rows report through out params.
struct rows_state {
    int selected = -1;
    int clicked_row = -1;
    int activated_row = -1;
    ImRect rect[2];
};

void draw_rows(rows_state &s)
{
    const rv_editor_theme theme = rv_editor_theme_olive;
    const rv_editor_table_column cols[3] = { { "A" }, { "B" }, { "C" } };
    if (!rv_editor_table_begin("rows", cols, 3)) {
        return;
    }
    for (int i = 0; i < 2; ++i) {
        rv_editor_table_row();
        rv_editor_table_cell();
        const rv_editor_table_row_result res = rv_editor_table_row_select(i == 0 ? "##r0" : "##r1", s.selected == i, theme);
        s.rect[i] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (res.clicked) {
            s.clicked_row = i;
            s.selected = i;
        }
        if (res.activated) {
            s.activated_row = i;
        }
        ImGui::TextUnformatted("first");
        rv_editor_table_cell();
        ImGui::TextUnformatted("second");
        rv_editor_table_cell();
        ImGui::TextUnformatted("third");
    }
    rv_editor_table_end();
}

void rows_frame(rv_editor_test::rv_editor_test_imgui &gui, rows_state &s)
{
    gui.frame([&]() {
        draw_rows(s);
    });
}

void rows_warm(rv_editor_test::rv_editor_test_imgui &gui, rows_state &s)
{
    for (int i = 0; i < 3; ++i) {
        rows_frame(gui, s);
    }
}

void press(rv_editor_test::rv_editor_test_imgui &gui, rows_state &s, bool down)
{
    ImGui::GetIO().AddMouseButtonEvent(0, down);
    rows_frame(gui, s);
}

} // namespace

// AC3: a single click anywhere in the row (first, middle, last column, the corners) selects that row only.
RV_EDITOR_TEST(rv_editor_test_click_area_row_single_click_selects)
{
    rv_editor_test::rv_editor_test_imgui gui;
    rows_state s;
    rows_warm(gui, s);
    const ImRect row = s.rect[1];
    const float mid_y = (row.Min.y + row.Max.y) / 2;
    const ImVec2 pts[5] = { { row.Min.x + inset, row.Min.y + inset },
        { row.Max.x - inset, row.Min.y + inset },
        { row.Min.x + inset, row.Max.y - inset },
        { row.Max.x - inset, row.Max.y - inset },
        { (row.Min.x + row.Max.x) / 2, mid_y } };
    for (const ImVec2 &p : pts) {
        s.clicked_row = -1;
        ImGui::GetIO().AddMousePosEvent(p.x, p.y);
        rows_frame(gui, s);
        press(gui, s, true);
        press(gui, s, false);
        rows_frame(gui, s);
        RV_EDITOR_CHECK(s.clicked_row == 1);
        RV_EDITOR_CHECK(s.activated_row == -1);
    }
    // The last column's text is inside the row's area.
    RV_EDITOR_CHECK(row.Max.x > 300.0f);
}

RV_EDITOR_TEST(rv_editor_test_click_area_row_double_click_acts)
{
    rv_editor_test::rv_editor_test_imgui gui;
    rows_state s;
    rows_warm(gui, s);
    const ImRect row = s.rect[0];
    ImGui::GetIO().AddMousePosEvent(row.Max.x - 10.0f, (row.Min.y + row.Max.y) / 2);
    rows_frame(gui, s);
    press(gui, s, true);
    press(gui, s, false);
    RV_EDITOR_CHECK(s.activated_row == -1);
    press(gui, s, true);
    press(gui, s, false);
    rows_frame(gui, s);
    RV_EDITOR_CHECK(s.activated_row == 0);
}

RV_EDITOR_TEST(rv_editor_test_click_area_row_enter_acts)
{
    rv_editor_test::rv_editor_test_imgui gui;
    rows_state s;
    rows_warm(gui, s);
    const ImRect row = s.rect[1];
    ImGui::GetIO().AddMousePosEvent((row.Min.x + row.Max.x) / 2, (row.Min.y + row.Max.y) / 2);
    rows_frame(gui, s);
    press(gui, s, true);
    press(gui, s, false);
    rows_frame(gui, s);
    RV_EDITOR_CHECK(s.activated_row == -1);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    rows_frame(gui, s);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    rows_frame(gui, s);
    RV_EDITOR_CHECK(s.activated_row == 1);
}

RV_EDITOR_TEST(rv_editor_test_click_area_row_selected_state_drawn)
{
    rv_editor_test::rv_editor_test_imgui gui;
    rows_state s;
    s.selected = 0;
    rows_warm(gui, s);
    RV_EDITOR_CHECK(s.rect[0].GetWidth() > 300.0f && s.rect[1].GetWidth() > 300.0f);
    RV_EDITOR_CHECK(s.rect[1].Min.y >= s.rect[0].Max.y);
}
