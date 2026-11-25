// Status bar segments keep their widths whatever the build or session says and show their text whole; a tile's content does not
// move when a second tab appears; a tab strip wider than its tile scrolls with the wheel and has no scrollbar; a pane without a
// title clears the stale one.

#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr float bar_width = 1280.0f;
constexpr int tab_count = 12;
constexpr float tile_width = 300.0f;

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

// One frame of the bar with `fields` in a window of `width`; `widest` (default: the fields) are the segments' templates.
rv_editor_status_probe bar_frame(rv_editor_test::rv_editor_test_imgui &gui,
    std::vector<std::string> fields,
    std::vector<std::string> widest = {},
    float width = bar_width)
{
    if (widest.empty()) {
        widest = fields;
    }
    std::vector<const char *> c;
    std::vector<const char *> w;
    for (size_t i = 0; i < fields.size(); ++i) {
        c.push_back(fields[i].c_str());
        w.push_back(widest[i].c_str());
    }
    rv_editor_status_probe probe;
    gui.frame(
        [&] {
            rv_editor_status_bar(c.data(), w.data(), static_cast<int>(c.size()), theme, &probe);
        },
        width);
    return probe;
}

// The templates of a project bar with its fields at their limits (R5).
std::vector<std::string> limits()
{
    return { "My Project",
        "Tools: 999+ missing",
        "Build #999999 step 99/99: " + std::string(16, 'W'),
        "Runtime: running, session 999999" };
}

std::string long_text()
{
    return std::string(100, 'W');
}

// "" when both draws have the same segment edges.
std::string same_segments(const rv_editor_status_probe &a, const rv_editor_status_probe &b)
{
    if (a.segments.size() != b.segments.size()) {
        return "segment count " + std::to_string(a.segments.size()) + " became " + std::to_string(b.segments.size());
    }
    for (size_t i = 0; i < a.segments.size(); ++i) {
        if (a.segments[i].x0 != b.segments[i].x0 || a.segments[i].x1 != b.segments[i].x1) {
            return "segment " + std::to_string(i) + " moved or resized";
        }
    }
    return "";
}

} // namespace

// AC14: no build, build at step 2 of 5, build failed, session live: the same segment edges.
RV_EDITOR_TEST(rv_editor_test_status_bar_segments_constant_across_states)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const auto w = limits();
    const rv_editor_status_probe idle = bar_frame(gui, { "My Project", "Tools: ready", "Build: none", "Runtime: stopped" }, w);
    const rv_editor_status_probe step =
        bar_frame(gui, { "My Project", "Tools: ready", "Build #3 step 2/5: compiling", "Runtime: stopped" }, w);
    const rv_editor_status_probe failed =
        bar_frame(gui, { "My Project", "Tools: 2 missing", "Build #3 step 2/5: compiling", "Runtime: stopped" }, w);
    const rv_editor_status_probe live =
        bar_frame(gui, { "My Project", "Tools: ready", "Build #3", "Runtime: running, session 2" }, w);
    RV_EDITOR_CHECK(idle.segments.size() == 4);
    RV_EDITOR_CHECK(same_segments(idle, step).empty());
    RV_EDITOR_CHECK(same_segments(idle, failed).empty());
    RV_EDITOR_CHECK(same_segments(idle, live).empty());
}

// The segments are laid left to right inside the bar, without overlap, and the last ends at the bar's edge.
RV_EDITOR_TEST(rv_editor_test_status_bar_segments_fill_the_bar_in_order)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_status_probe p =
        bar_frame(gui, { "My Project", "Tools: ready", "Build: none", "Runtime: stopped" }, limits());
    RV_EDITOR_CHECK(p.segments.size() == 4);
    for (size_t i = 0; i < p.segments.size(); ++i) {
        RV_EDITOR_CHECK(p.segments[i].x1 > p.segments[i].x0);
        RV_EDITOR_CHECK(i == 0 || p.segments[i].x0 >= p.segments[i - 1].x1);
    }
    if (!p.segments.empty()) {
        RV_EDITOR_CHECK(p.segments.back().x1 <= bar_width);
        RV_EDITOR_CHECK(p.segments.back().x1 >= bar_width - 20.0f);
    }
}

// The same holds without a project: two segments, and the longest text in either does not change the edges.
RV_EDITOR_TEST(rv_editor_test_status_bar_two_segments_constant)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const std::vector<std::string> w = { long_text(), "Tools: 999+ missing" };
    const rv_editor_status_probe a = bar_frame(gui, { "No project", "Tools: ready" }, w);
    const rv_editor_status_probe b = bar_frame(gui, { long_text(), "Tools: 2 missing" }, w);
    RV_EDITOR_CHECK(a.segments.size() == 2);
    RV_EDITOR_CHECK(same_segments(a, b).empty());
}

// R17/AC18: the longest text of a segment is shown whole: no "...", the segment holds it, no tooltip.
RV_EDITOR_TEST(rv_editor_test_status_bar_longest_text_is_shown_whole)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const std::string text = long_text();
    const rv_editor_status_probe p = bar_frame(gui, { "My Project", "Tools: ready", text, "Runtime: stopped" });
    RV_EDITOR_CHECK(p.segments.size() == 4);
    if (p.segments.size() == 4) {
        const auto &s = p.segments[2];
        RV_EDITOR_CHECK(s.shown == text);
        RV_EDITOR_CHECK(s.shown.find("...") == std::string::npos);
        RV_EDITOR_CHECK(s.tooltip.empty());
        gui.frame([&] {
            RV_EDITOR_CHECK(ImGui::CalcTextSize(s.shown.c_str()).x <= s.x1 - s.x0);
        });
    }
}

// A text that fits is shown as it is, without a tooltip.
RV_EDITOR_TEST(rv_editor_test_status_bar_fitting_text_has_no_tooltip)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_status_probe p =
        bar_frame(gui, { "My Project", "Tools: ready", "Build: none", "Runtime: stopped" }, limits());
    RV_EDITOR_CHECK(p.segments.size() == 4);
    for (const auto &s : p.segments) {
        RV_EDITOR_CHECK(!s.shown.empty());
        RV_EDITOR_CHECK(s.tooltip.empty());
    }
    RV_EDITOR_CHECK(p.segments.size() == 4 && p.segments[2].shown == "Build: none");
}

// Hovering a segment shows no tooltip: nothing in the bar is cut, so there is nothing to add.
RV_EDITOR_TEST(rv_editor_test_status_bar_hover_shows_no_tooltip)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    const std::vector<std::string> f = { "My Project", "Tools: ready", long_text(), "Runtime: stopped" };
    rv_editor_status_probe p = bar_frame(gui, f);
    RV_EDITOR_CHECK(p.segments.size() == 4);
    if (p.segments.size() != 4) {
        return;
    }
    const ImVec2 mouse((p.segments[2].x0 + p.segments[2].x1) / 2.0f, 8.0f);
    for (int i = 0; i < 3; ++i) {
        ImGui::GetIO().MousePos = mouse;
        bar_frame(gui, f);
    }
    const ImGuiWindow *tip = ImGui::FindWindowByName("##Tooltip_00");
    RV_EDITOR_CHECK(tip == nullptr || !tip->WasActive);
}

namespace
{

struct workspace_fixture {
    rv_editor_workspace ws;
    ImVec2 pos, size; // the content window of the active pane, at the last draw
    workspace_fixture()
    {
        const rv_editor_pane_id a = rv_editor_pane_add(ws.panes, rv_editor_pane_kind::output);
        ws.layout = rv_editor_layout_make(a);
        ws.focused_leaf = ws.layout.root;
    }
    uint32_t leaf() const
    {
        return ws.layout.root;
    }
};

void draw_pane(void *context, rv_editor_pane_id, rv_editor_pane_kind, const rv_editor_theme &)
{
    auto *f = static_cast<workspace_fixture *>(context);
    f->pos = ImGui::GetWindowPos();
    f->size = ImGui::GetWindowSize();
}

// Draws the workspace for a few frames so the child windows settle.
void draw_workspace(rv_editor_test::rv_editor_test_imgui &gui, workspace_fixture &f)
{
    for (int i = 0; i < 3; ++i) {
        gui.frame([&] {
            std::string error;
            rv_editor_workspace_draw(f.ws, theme, draw_pane, nullptr, &f, { 0, 0, 400, 300 }, error);
        });
    }
}

bool same_content(const workspace_fixture &a, const workspace_fixture &b)
{
    return a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.size.x == b.size.x && a.size.y == b.size.y;
}

} // namespace

// AC15: the tile's content rectangle is the same with one tab, with two and with one again after the second closes
// (the tab row is always reserved).
RV_EDITOR_TEST(rv_editor_test_tile_content_same_with_one_and_two_tabs)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    workspace_fixture f;
    draw_workspace(gui, f);
    const workspace_fixture one = [&] {
        workspace_fixture c;
        c.pos = f.pos;
        c.size = f.size;
        return c;
    }();
    RV_EDITOR_CHECK(one.size.y > 0.0f);

    const rv_editor_pane_id b = rv_editor_pane_add(f.ws.panes, rv_editor_pane_kind::inspector_report);
    RV_EDITOR_CHECK(rv_editor_tile_insert(f.ws.layout, f.leaf(), b, rv_editor_tile_dock::tab) == f.leaf());
    draw_workspace(gui, f);
    RV_EDITOR_CHECK(f.ws.layout.nodes[f.leaf()].leaf.tabs.size() == 2);
    RV_EDITOR_CHECK(same_content(one, f));

    RV_EDITOR_CHECK(rv_editor_tile_remove(f.ws.layout, b) == 0);
    draw_workspace(gui, f);
    RV_EDITOR_CHECK(same_content(one, f));
}

// Twelve tabs do not change the content either.
RV_EDITOR_TEST(rv_editor_test_tile_content_same_with_many_tabs)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    workspace_fixture f;
    draw_workspace(gui, f);
    const ImVec2 size = f.size;
    for (int i = 1; i < tab_count; ++i) {
        const rv_editor_pane_id p = rv_editor_pane_add(f.ws.panes, rv_editor_pane_kind::inspector_report);
        rv_editor_tile_insert(f.ws.layout, f.leaf(), p, rv_editor_tile_dock::tab);
    }
    draw_workspace(gui, f);
    RV_EDITOR_CHECK(f.ws.layout.nodes[f.leaf()].leaf.tabs.size() == static_cast<size_t>(tab_count));
    RV_EDITOR_CHECK(f.size.x == size.x && f.size.y == size.y);
}

namespace
{

struct strip_fixture {
    std::vector<std::string> names;
    std::vector<const char *> labels;
    int active = 0;
    bool changed = false;
    rv_editor_tab_probe probe;
    explicit strip_fixture(int n)
    {
        for (int i = 0; i < n; ++i) {
            names.push_back("Tab number " + std::to_string(i));
        }
        for (const auto &s : names) {
            labels.push_back(s.c_str());
        }
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
    bool visible(size_t i) const
    {
        return i < probe.tabs.size() && probe.tabs[i].x0 >= probe.strip_x0 && probe.tabs[i].x1 <= probe.strip_x1;
    }
};

} // namespace

// AC15: the mouse wheel over a strip that does not fit scrolls it until a far tab is visible.
RV_EDITOR_TEST(rv_editor_test_tab_strip_wheel_scrolls_to_far_tab)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    strip_fixture s(tab_count);
    s.frame(gui);
    RV_EDITOR_CHECK(s.probe.tabs.size() == static_cast<size_t>(tab_count));
    if (s.probe.tabs.size() != static_cast<size_t>(tab_count)) {
        return;
    }
    RV_EDITOR_CHECK(!s.visible(tab_count - 1));
    const ImVec2 over((s.probe.strip_x0 + s.probe.strip_x1) / 2.0f, (s.probe.tabs[0].y0 + s.probe.tabs[0].y1) / 2.0f);
    ImGui::GetIO().AddMousePosEvent(over.x, over.y);
    s.frame(gui);
    for (int i = 0; i < 40 && !s.visible(tab_count - 1); ++i) {
        ImGui::GetIO().AddMouseWheelEvent(0.0f, -1.0f);
        s.frame(gui);
    }
    RV_EDITOR_CHECK(s.probe.scroll_x > 0.0f);
    RV_EDITOR_CHECK(s.visible(tab_count - 1));
}

// A wheel over the strip does not scroll it back past the first tab.
RV_EDITOR_TEST(rv_editor_test_tab_strip_wheel_up_stays_at_start)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    strip_fixture s(tab_count);
    s.frame(gui);
    RV_EDITOR_CHECK(s.probe.tabs.size() == static_cast<size_t>(tab_count));
    if (s.probe.tabs.empty()) {
        return;
    }
    ImGui::GetIO().AddMousePosEvent((s.probe.strip_x0 + s.probe.strip_x1) / 2.0f,
        (s.probe.tabs[0].y0 + s.probe.tabs[0].y1) / 2.0f);
    s.frame(gui);
    ImGui::GetIO().AddMouseWheelEvent(0.0f, 1.0f);
    s.frame(gui);
    RV_EDITOR_CHECK(s.probe.scroll_x == 0.0f);
    RV_EDITOR_CHECK(s.visible(0));
}

// AC4/R7: a strip has no scrollbar, whether its tabs fit or not (arrows and a list replace it).
RV_EDITOR_TEST(rv_editor_test_tab_strip_never_has_a_scrollbar)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    strip_fixture many(tab_count);
    many.frame(gui);
    many.frame(gui);
    RV_EDITOR_CHECK(many.probe.tabs.size() == static_cast<size_t>(tab_count));
    RV_EDITOR_CHECK(!many.probe.scrollbar);
    strip_fixture few(2);
    few.frame(gui);
    few.frame(gui);
    RV_EDITOR_CHECK(few.probe.tabs.size() == 2);
    RV_EDITOR_CHECK(!few.probe.scrollbar);
}

// AC15: a tab scrolled into view activates on a click.
RV_EDITOR_TEST(rv_editor_test_tab_strip_scrolled_tab_activates_on_click)
{
    load_texts();
    rv_editor_test::rv_editor_test_imgui gui;
    strip_fixture s(tab_count);
    s.frame(gui);
    RV_EDITOR_CHECK(s.probe.tabs.size() == static_cast<size_t>(tab_count));
    if (s.probe.tabs.size() != static_cast<size_t>(tab_count)) {
        return;
    }
    ImGui::GetIO().AddMousePosEvent((s.probe.strip_x0 + s.probe.strip_x1) / 2.0f,
        (s.probe.tabs[0].y0 + s.probe.tabs[0].y1) / 2.0f);
    s.frame(gui);
    for (int i = 0; i < 40 && !s.visible(tab_count - 1); ++i) {
        ImGui::GetIO().AddMouseWheelEvent(0.0f, -1.0f);
        s.frame(gui);
    }
    RV_EDITOR_CHECK(s.visible(tab_count - 1));
    const auto &t = s.probe.tabs.back();
    ImGui::GetIO().AddMousePosEvent((t.x0 + t.x1) / 2.0f, (t.y0 + t.y1) / 2.0f);
    s.frame(gui);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    s.frame(gui);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    s.frame(gui);
    s.frame(gui);
    RV_EDITOR_CHECK(s.active == tab_count - 1);
    RV_EDITOR_CHECK(s.changed);
}

// A pane that has no title now (its terminal is gone) loses the title it had, so the tab shows the kind's name.
RV_EDITOR_TEST(rv_editor_test_shell_title_clears_stale_title)
{
    load_texts();
    rv_editor_shell shell;
    const rv_editor_pane_id pane = 1;
    shell.ws.titles[pane] = "stale title";
    rv_editor_shell_title(shell, pane, rv_editor_pane_kind::terminal);
    RV_EDITOR_CHECK(shell.ws.titles.find(pane) == shell.ws.titles.end());
}
