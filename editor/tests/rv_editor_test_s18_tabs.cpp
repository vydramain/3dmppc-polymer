// S18, R7/R2, AC8: the file tabs of a code tile are the tile-tab row (twelve of them in 300 px: arrows and a list, the
// active tab in view), each tab's label and the status line's text show the whole file path (no "..."), the modified
// marker last, and the full path stays the tooltip.

#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "panes/rv_editor_panes.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s18.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr float tile_width = 300.0f;
constexpr int file_count = 12;
constexpr int long_file = 3;
constexpr int click_limit = 60;
const std::string root = "/home/user/project";

std::string long_name()
{
    return root + "/scripts/a_very_long_directory_name/another_long_directory/engine_settings.cfg";
}

std::vector<rv_editor_code_tab_file> files()
{
    std::vector<rv_editor_code_tab_file> v;
    for (int i = 0; i < file_count; ++i) {
        v.push_back({ i == long_file ? long_name() : root + "/src/file_" + std::to_string(i) + ".lua", i == 2 });
    }
    return v;
}

bool present(const rv_editor_tab_probe::rect &r)
{
    return r.x1 > r.x0 && r.y1 > r.y0;
}

struct strip_fixture {
    std::vector<rv_editor_code_tab_file> list = files();
    int active = 0;
    bool changed = false;
    rv_editor_tab_probe probe;

    void frame(rv_editor_test_imgui &gui)
    {
        probe = {};
        gui.frame(
            [&] {
                changed |= rv_editor_code_tab_strip(list, root, &active, theme, &probe);
            },
            tile_width);
    }
    void settle(rv_editor_test_imgui &gui)
    {
        for (int i = 0; i < 3; ++i) {
            frame(gui);
        }
    }
    bool visible(size_t i) const
    {
        return i < probe.tabs.size() && probe.tabs[i].x0 >= probe.strip_x0 && probe.tabs[i].x1 <= probe.strip_x1;
    }
    // A tab that fits the strip must lie in it whole; a wider one is reached once it occupies the visible part.
    bool reached(size_t i) const
    {
        if (i >= probe.tabs.size()) {
            return false;
        }
        const rv_editor_tab_probe::rect &t = probe.tabs[i];
        if (t.x1 - t.x0 <= probe.strip_x1 - probe.strip_x0) {
            return visible(i);
        }
        return t.x0 < probe.strip_x1 && t.x1 > probe.strip_x0;
    }
    void click(rv_editor_test_imgui &gui, const rv_editor_tab_probe::rect &r)
    {
        ImGui::GetIO().AddMousePosEvent((r.x0 + r.x1) / 2.0f, (r.y0 + r.y1) / 2.0f);
        frame(gui);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame(gui);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        settle(gui);
    }
};

} // namespace

// AC8: twelve files in 300 px: `<` `>` and `v` inside the tile, no scrollbar, one tab-row high, every file reachable.
RV_EDITOR_TEST(rv_editor_test_s18_twelve_file_tabs_overflow_into_arrows_and_a_list)
{
    s18::load_texts();
    rv_editor_test_imgui gui;
    strip_fixture s;
    s.settle(gui);
    RV_EDITOR_CHECK(s.probe.tabs.size() == static_cast<size_t>(file_count));
    RV_EDITOR_CHECK(present(s.probe.prev) && present(s.probe.next) && present(s.probe.list));
    RV_EDITOR_CHECK(s.probe.list.x1 <= tile_width);
    RV_EDITOR_CHECK(!s.probe.scrollbar);
    float frame_h = 0.0f;
    gui.frame([&] {
        frame_h = ImGui::GetFrameHeight();
    });
    RV_EDITOR_CHECK(s.probe.strip_y1 - s.probe.strip_y0 == frame_h);
    if (!present(s.probe.next)) {
        return;
    }
    for (size_t i = 0; i < static_cast<size_t>(file_count); ++i) {
        for (int k = 0; k < click_limit && !s.reached(i); ++k) {
            s.click(gui, s.probe.next);
        }
        RV_EDITOR_CHECK(s.reached(i));
    }
    for (size_t i = static_cast<size_t>(file_count); i-- > 0;) {
        for (int k = 0; k < click_limit && !s.reached(i); ++k) {
            s.click(gui, s.probe.prev);
        }
        RV_EDITOR_CHECK(s.reached(i));
    }
}

// R7: the list opens with all twelve files; choosing one activates it, returns true and brings it into view.
RV_EDITOR_TEST(rv_editor_test_s18_the_list_picks_a_file_and_the_active_tab_is_in_view)
{
    s18::load_texts();
    rv_editor_test_imgui gui;
    strip_fixture s;
    s.settle(gui);
    RV_EDITOR_CHECK(present(s.probe.list));
    if (!present(s.probe.list)) {
        return;
    }
    s.click(gui, s.probe.list);
    RV_EDITOR_CHECK(s.probe.list_open && s.probe.list_items.size() == static_cast<size_t>(file_count));
    if (s.probe.list_items.size() != static_cast<size_t>(file_count)) {
        return;
    }
    s.click(gui, s.probe.list_items[file_count - 1]);
    RV_EDITOR_CHECK(s.changed && s.active == file_count - 1);
    RV_EDITOR_CHECK(s.visible(file_count - 1));
    // An active file set from outside (focus moved) is scrolled into view too.
    s.active = 1;
    s.settle(gui);
    RV_EDITOR_CHECK(s.visible(1));
}

// R2: a tab shows its whole path (relative to the root) and the modified marker last; no tab ends in "...".
RV_EDITOR_TEST(rv_editor_test_s18_file_tabs_show_the_whole_path_and_the_marker)
{
    s18::load_texts();
    rv_editor_test_imgui gui;
    strip_fixture s;
    s.settle(gui);
    RV_EDITOR_CHECK(s.probe.shown.size() == static_cast<size_t>(file_count));
    if (s.probe.shown.size() != static_cast<size_t>(file_count)) {
        return;
    }
    const std::string marker = rv_editor_text("pane_code.file_modified_marker");
    RV_EDITOR_CHECK(
        s.probe.shown[long_file] == "scripts/a_very_long_directory_name/another_long_directory/engine_settings.cfg");
    RV_EDITOR_CHECK(s.probe.shown[2] == "src/file_2.lua" + marker);
    for (const std::string &t : s.probe.shown) {
        RV_EDITOR_CHECK(t.find("...") == std::string::npos);
    }
    float text_w = 0.0f;
    gui.frame([&] {
        text_w = ImGui::CalcTextSize(s.probe.shown[long_file].c_str()).x;
    });
    RV_EDITOR_CHECK(s.probe.tabs[long_file].x1 - s.probe.tabs[long_file].x0 >= text_w);
}

// R2: the text of a tab and of the status line is the whole path whatever the width given; the tooltip is the full path.
RV_EDITOR_TEST(rv_editor_test_s18_tab_and_status_texts_are_whole_at_any_width)
{
    s18::load_texts();
    rv_editor_test_imgui gui;
    const std::string name = long_name();
    const std::string marker = rv_editor_text("pane_code.file_modified_marker");
    const std::string whole = "scripts/a_very_long_directory_name/another_long_directory/engine_settings.cfg";
    for (const float width : { 50.0f, 200.0f, 4000.0f }) {
        rv_editor_code_text tab, status;
        gui.frame([&] {
            tab = rv_editor_code_tab_text(name, root, true, width);
            status = rv_editor_code_status_text(name, root, true, width);
        });
        RV_EDITOR_CHECK(tab.shown == whole + marker);
        RV_EDITOR_CHECK(status.shown == whole + marker);
        RV_EDITOR_CHECK(tab.tooltip == name && status.tooltip == name);
    }
}
