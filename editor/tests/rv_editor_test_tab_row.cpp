// R7: a tile's tab row is one tab tall with one tab and with twelve, has no scrollbar, shows `<` `>` and a list of every
// tab when the tabs overflow, keeps the active tab in view, and shows tab and tile titles whole; the tile minimum
// counts the row and the title; the frame of a tile fits whole.

#include <cstdio>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr int tab_count = 12;
constexpr float tile_width = 300.0f;
constexpr int click_limit = 60;

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

struct row_fixture {
    std::vector<std::string> names;
    std::vector<const char *> labels;
    int active = 0;
    bool changed = false;
    rv_editor_tab_probe probe;
    explicit row_fixture(std::vector<std::string> n)
        : names(std::move(n))
    {
        for (const auto &s : names) {
            labels.push_back(s.c_str());
        }
    }
    explicit row_fixture(int n)
        : row_fixture([&] {
            std::vector<std::string> v;
            for (int i = 0; i < n; ++i) {
                v.push_back("Tab number " + std::to_string(i));
            }
            return v;
        }())
    {
    }
    void frame(rv_editor_test::rv_editor_test_imgui &gui)
    {
        probe = {};
        gui.frame(
            [&] {
                changed |= rv_editor_tab_strip("##tabs",
                    labels.data(),
                    static_cast<int>(labels.size()),
                    &active,
                    theme,
                    {},
                    nullptr,
                    240.0f,
                    &probe);
            },
            tile_width);
    }
    void settle(rv_editor_test::rv_editor_test_imgui &gui)
    {
        for (int i = 0; i < 3; ++i) {
            frame(gui);
        }
    }
    bool visible(size_t i) const
    {
        return i < probe.tabs.size() && probe.tabs[i].x0 >= probe.strip_x0 && probe.tabs[i].x1 <= probe.strip_x1;
    }
    // A left click at the middle of `r`, then frames for the popup or the scroll to settle.
    void click(rv_editor_test::rv_editor_test_imgui &gui, const rv_editor_tab_probe::rect &r)
    {
        ImGui::GetIO().AddMousePosEvent((r.x0 + r.x1) / 2.0f, (r.y0 + r.y1) / 2.0f);
        frame(gui);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame(gui);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        settle(gui);
    }
};

bool present(const rv_editor_tab_probe::rect &r)
{
    return r.x1 > r.x0 && r.y1 > r.y0;
}

// The tile fixture of the workspace tests: one leaf of `tabs` Problems panes, the first one Output.
struct tile_fixture {
    rv_editor_workspace ws;
    ImVec2 pane_size;     // the content window of the active pane at the last draw
    float leaf_width = 0; // the window the header and the content sit in
    explicit tile_fixture(int tabs)
    {
        const rv_editor_pane_id a = rv_editor_pane_add(ws.panes, rv_editor_pane_kind::output);
        ws.layout = rv_editor_layout_make(a);
        ws.focused_leaf = ws.layout.root;
        for (int i = 1; i < tabs; ++i) {
            const rv_editor_pane_id p = rv_editor_pane_add(ws.panes, rv_editor_pane_kind::inspector_report);
            rv_editor_tile_insert(ws.layout, ws.layout.root, p, rv_editor_tile_dock::tab);
        }
        ws.layout.nodes[ws.layout.root].leaf.active = 0;
        ws.minimums[a] = { min_w, min_h };
    }
    static constexpr int min_w = 200;
    static constexpr int min_h = 100;
};

void draw_pane(void *context, rv_editor_pane_id, rv_editor_pane_kind, const rv_editor_theme &)
{
    auto *f = static_cast<tile_fixture *>(context);
    f->pane_size = ImGui::GetWindowSize();
    f->leaf_width = ImGui::GetCurrentWindow()->ParentWindow->Size.x;
}

void draw_tile(rv_editor_test::rv_editor_test_imgui &gui, tile_fixture &f, rv_editor_rect area)
{
    for (int i = 0; i < 3; ++i) {
        gui.frame(
            [&] {
                std::string error;
                rv_editor_workspace_draw(f.ws, theme, draw_pane, nullptr, &f, area, error);
            },
            static_cast<float>(area.w),
            static_cast<float>(area.h));
    }
}

// The workspace's own scroll area wraps every tile, which would hide all of it from fits_whole; only the body of the
// tile (the "##leaf.../##scroll" window and what lies inside it) stays a scroll area.
rv_editor_test::rv_editor_probe_record tile_only(rv_editor_test::rv_editor_probe_record record)
{
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (const auto &e : record.entries) {
        const size_t leaf = e.label.find("/##leaf_");
        if (leaf != std::string::npos && e.label.find("/##scroll_", leaf) != std::string::npos) {
            x0 = e.x0, y0 = e.y0, x1 = e.x1, y1 = e.y1;
        }
    }
    for (auto &e : record.entries) {
        e.in_scroll = e.x0 >= x0 && e.y0 >= y0 && e.x1 <= x1 && e.y1 <= y1 && x1 > x0;
    }
    return record;
}

} // namespace

// AC4: the row is exactly one tab (FrameHeight) high with one tab and with twelve, and has no scrollbar under it.
RV_EDITOR_TEST(rv_editor_test_tab_row_is_one_frame_high_with_one_and_many_tabs)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    float frame_h = 0.0f;
    float strip_h = 0.0f;
    gui.frame([&] {
        frame_h = ImGui::GetFrameHeight();
        strip_h = rv_editor_tab_strip_height();
    });
    RV_EDITOR_CHECK(strip_h == frame_h);
    for (const int n : { 1, tab_count }) {
        row_fixture s(n);
        s.settle(gui);
        RV_EDITOR_CHECK(s.probe.tabs.size() == static_cast<size_t>(n));
        RV_EDITOR_CHECK(s.probe.strip_y1 - s.probe.strip_y0 == frame_h);
        RV_EDITOR_CHECK(!s.probe.tabs.empty() && s.probe.tabs[0].y1 - s.probe.tabs[0].y0 == frame_h);
        RV_EDITOR_CHECK(!s.probe.scrollbar);
    }
}

// AC8: twelve tabs in 300 px show `<`, `>` and `v` inside the tile, and the tabs stay between the buttons; tabs that
// fit show none of them.
RV_EDITOR_TEST(rv_editor_test_tab_row_overflow_shows_arrows_and_list_button)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    row_fixture many(tab_count);
    many.settle(gui);
    RV_EDITOR_CHECK(present(many.probe.prev));
    RV_EDITOR_CHECK(present(many.probe.next));
    RV_EDITOR_CHECK(present(many.probe.list));
    RV_EDITOR_CHECK(many.probe.prev.x0 >= 0.0f && many.probe.list.x1 <= tile_width);
    RV_EDITOR_CHECK(many.probe.strip_x0 >= many.probe.prev.x1);
    RV_EDITOR_CHECK(many.probe.strip_x1 <= many.probe.next.x0 && many.probe.strip_x1 <= many.probe.list.x0);
    row_fixture few(2);
    few.settle(gui);
    RV_EDITOR_CHECK(!present(few.probe.prev) && !present(few.probe.next) && !present(few.probe.list));
}

// AC8: the arrows bring every tab into view, forward and back, without changing the active tab.
RV_EDITOR_TEST(rv_editor_test_tab_row_arrows_reach_every_tab)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    row_fixture s(tab_count);
    s.settle(gui);
    RV_EDITOR_CHECK(present(s.probe.next));
    if (!present(s.probe.next)) {
        return;
    }
    for (size_t i = 0; i < static_cast<size_t>(tab_count); ++i) {
        for (int k = 0; k < click_limit && !s.visible(i); ++k) {
            s.click(gui, s.probe.next);
        }
        RV_EDITOR_CHECK(s.visible(i));
    }
    for (size_t i = tab_count; i-- > 0;) {
        for (int k = 0; k < click_limit && !s.visible(i); ++k) {
            s.click(gui, s.probe.prev);
        }
        RV_EDITOR_CHECK(s.visible(i));
    }
    RV_EDITOR_CHECK(s.active == 0 && !s.changed);
}

// AC8: `v` opens a list of all twelve tabs; choosing one activates it and scrolls it fully into view.
RV_EDITOR_TEST(rv_editor_test_tab_row_list_selects_any_tab_and_shows_it)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    row_fixture s(tab_count);
    s.settle(gui);
    RV_EDITOR_CHECK(present(s.probe.list));
    if (!present(s.probe.list)) {
        return;
    }
    for (const size_t target : { static_cast<size_t>(tab_count - 1), size_t(5), size_t(0) }) {
        s.click(gui, s.probe.list);
        RV_EDITOR_CHECK(s.probe.list_open);
        RV_EDITOR_CHECK(s.probe.list_items.size() == static_cast<size_t>(tab_count));
        if (s.probe.list_items.size() != static_cast<size_t>(tab_count)) {
            return;
        }
        s.click(gui, s.probe.list_items[target]);
        RV_EDITOR_CHECK(s.active == static_cast<int>(target));
        RV_EDITOR_CHECK(!s.probe.list_open);
        RV_EDITOR_CHECK(s.visible(target));
    }
    RV_EDITOR_CHECK(s.changed);
}

// R7: an active tab set from outside (a pane focused elsewhere) is scrolled into view.
RV_EDITOR_TEST(rv_editor_test_tab_row_scrolls_the_active_tab_into_view)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    row_fixture s(tab_count);
    s.settle(gui);
    RV_EDITOR_CHECK(!s.visible(tab_count - 1));
    s.active = tab_count - 1;
    s.settle(gui);
    RV_EDITOR_CHECK(s.visible(tab_count - 1));
}

// AC8: a 60-character tab title is drawn whole (no "...", the tab as wide as its text); after the row is scrolled to
// it, the title is in view as far as the row allows.
RV_EDITOR_TEST(rv_editor_test_tab_row_long_title_shows_whole)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const std::string title = "Sixty characters of a long tab title, every one of them kept";
    std::vector<std::string> names{ "One", "Two", "Three", title, "Five" };
    row_fixture s(names);
    s.settle(gui);
    RV_EDITOR_CHECK(title.size() >= 59 && title.size() <= 60);
    RV_EDITOR_CHECK(s.probe.shown.size() == names.size());
    if (s.probe.shown.size() != names.size() || s.probe.tabs.size() != names.size()) {
        return;
    }
    RV_EDITOR_CHECK(s.probe.shown[3] == title);
    float text_w = 0.0f;
    gui.frame([&] {
        text_w = ImGui::CalcTextSize(title.c_str()).x;
    });
    RV_EDITOR_CHECK(s.probe.tabs[3].x1 - s.probe.tabs[3].x0 >= text_w);
    for (int k = 0; k < click_limit && s.probe.tabs[3].x1 > s.probe.strip_x1 && present(s.probe.next); ++k) {
        s.click(gui, s.probe.next);
    }
    // Wider than the row: its end is in view; narrower: all of it.
    const auto &t = s.probe.tabs[3];
    RV_EDITOR_CHECK(t.x1 <= s.probe.strip_x1);
    RV_EDITOR_CHECK(t.x1 - t.x0 > s.probe.strip_x1 - s.probe.strip_x0 || t.x0 >= s.probe.strip_x0);
}

// AC4/R7: the tile's content window is as high as its minimum with one tab, so the minimum counts the tab row; it
// does not change with two or twelve tabs.
RV_EDITOR_TEST(rv_editor_test_tile_minimum_counts_the_tab_row_with_one_tab)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    float height_of_one = 0.0f;
    for (const int tabs : { 1, 2, tab_count }) {
        tile_fixture f(tabs);
        draw_tile(gui, f, { 0, 0, 20, 20 }); // smaller than any minimum: the tile takes its minimum
        RV_EDITOR_CHECK(f.pane_size.y >= static_cast<float>(tile_fixture::min_h));
        RV_EDITOR_CHECK(f.pane_size.x >= static_cast<float>(tile_fixture::min_w));
        if (tabs == 1) {
            height_of_one = f.pane_size.y;
        }
        RV_EDITOR_CHECK(f.pane_size.y == height_of_one);
    }
}

// R1/R7: the tile is at least as wide as its title plus the X and M boxes, so a 60-character title is not cut.
RV_EDITOR_TEST(rv_editor_test_tile_minimum_width_holds_the_whole_title)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    tile_fixture f(1);
    const std::string title = "Sixty characters of a long tile title, every one of them kept";
    f.ws.titles[f.ws.layout.nodes[f.ws.layout.root].leaf.tabs[0]] = title;
    draw_tile(gui, f, { 0, 0, 20, 20 });
    float text_w = 0.0f;
    float box = 0.0f;
    gui.frame([&] {
        text_w = ImGui::CalcTextSize(title.c_str()).x;
        box = ImGui::GetFrameHeight() - 2.0f * static_cast<float>(theme.bevel_px * theme.scale);
    });
    RV_EDITOR_CHECK(f.leaf_width >= text_w + 2.0f * box);
}

// fits_whole: the frame of a tile (header with X/M, tab row with arrows, outline) lies inside the tile, with one tab
// and with twelve, with a long title, in 300 px; a scrolled-out tab is not an element of the tile.
RV_EDITOR_TEST(rv_editor_test_tile_frame_fits_whole)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    for (const int tabs : { 1, tab_count }) {
        tile_fixture f(tabs);
        f.ws.titles[f.ws.layout.nodes[f.ws.layout.root].leaf.tabs[0]] = "A tile title of some length";
        const rv_editor_rect area{ 0, 0, 300, 300 };
        draw_tile(gui, f, area);
        const auto record = tile_only(rv_editor_test::rv_editor_probe_draw(
            gui,
            300.0f,
            [&](rv_editor_test::rv_editor_probe_record &) {
                std::string error;
                rv_editor_workspace_draw(f.ws, theme, draw_pane, nullptr, &f, area, error);
            },
            300.0f));
        const std::string bad = rv_editor_test::rv_editor_fits_whole(record, ImRect(0, 0, 300, 300));
        RV_EDITOR_CHECK(bad.empty());
        if (!bad.empty()) {
            std::fprintf(stderr, "tabs %d: %s\n", tabs, bad.c_str());
        }
    }
}
