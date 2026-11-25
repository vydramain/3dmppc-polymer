// S21, R21, AC22, AC26: the Code, Scene, Debug and Burn starting layouts in a 1280x720 window at scale 1 with the real
// fonts: the tree's minimum is within the work area (no scroll bar) and every leaf is at least its minimum; a saved
// tree with a tile under its minimum opens with the tile widened; a window under the tree's minimum scrolls.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "layout/rv_editor_tile.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr float status_paddings = 2.0f; // main.cpp window_padding_sides
constexpr int settle_frames = 6;

struct preset_case {
    rv_editor_layout_preset preset;
    const char *name;
};
const preset_case presets[] = { { rv_editor_layout_preset::code, "Code" },
    { rv_editor_layout_preset::scene, "Scene" },
    { rv_editor_layout_preset::debug, "Debug" },
    { rv_editor_layout_preset::burn, "Burn" } };

struct drawn {
    std::map<rv_editor_pane_id, ImVec2> body; // the active tab's body, as the pane callback saw it
    rv_editor_size tree{ 0, 0 };              // the placed tree (at least its minimum)
    rv_editor_size area{ 0, 0 };
    ImVec2 scroll_max{ 0, 0 };
    bool found_scroll = false;
};

void record_body(void *context, rv_editor_pane_id id, rv_editor_pane_kind, const rv_editor_theme &)
{
    static_cast<drawn *>(context)->body[id] = ImGui::GetCurrentWindow()->Size;
}

// Draws the workspace the way the editor does: the menu bar and the status bar take their rows off the window.
drawn draw(rv_editor_test_imgui &gui, rv_editor_shell &shell, int w, int h)
{
    rv_editor_theme_apply(theme, ImGui::GetStyle());
    ImGui::GetStyle().FontSizeBase = rv_editor_font_ui()->LegacySize;
    drawn out;
    for (int frame = 0; frame < settle_frames; ++frame) {
        out = drawn();
        gui.frame(
            [&] {
                shell.ws.minimums.clear();
                for (size_t i = 0; i < shell.ws.panes.panes.size(); ++i) {
                    rv_editor_shell_set_minimum(shell,
                        static_cast<rv_editor_pane_id>(i),
                        shell.ws.panes.panes[i].kind,
                        { 0, 0 });
                }
                const float frame_h = ImGui::GetFrameHeight();
                const float bars = frame_h + frame_h + status_paddings * ImGui::GetStyle().WindowPadding.y;
                out.area = { w, static_cast<int>(static_cast<float>(h) - bars) };
                std::string error;
                rv_editor_workspace_draw(shell.ws,
                    theme,
                    record_body,
                    nullptr,
                    &out,
                    { 0, static_cast<int>(frame_h), out.area.w, out.area.h },
                    error);
                for (const ImGuiWindow *win : ImGui::GetCurrentContext()->Windows) {
                    if (win->Active && std::strstr(win->Name, "##scroll") != nullptr && win->ParentWindow != nullptr &&
                        win->ParentWindow->ParentWindow == nullptr) {
                        out.scroll_max = win->ScrollMax;
                        out.found_scroll = true;
                    }
                }
            },
            static_cast<float>(w),
            static_cast<float>(h));
        const rv_editor_rect &root = shell.ws.rects[shell.ws.layout.root];
        out.tree = { root.w, root.h };
    }
    return out;
}

rv_editor_size minimum_of(const rv_editor_shell &shell, rv_editor_pane_id pane)
{
    const auto it = shell.ws.minimums.find(pane);
    return it != shell.ws.minimums.end() ? it->second : rv_editor_size{ 0, 0 };
}

// Counts the leaves whose body is under the largest minimum of their tabs; prints every leaf (the biggest first).
int leaves_under_minimum(rv_editor_shell &shell, const drawn &d, const char *name)
{
    struct row {
        std::string kinds;
        rv_editor_size min;
        rv_editor_size rect;
        ImVec2 body;
    };
    std::vector<row> rows;
    int bad = 0;
    for (uint32_t n = 0; n < shell.ws.layout.nodes.size(); ++n) {
        const auto &node = shell.ws.layout.nodes[n];
        if (node.kind != rv_editor_tile_kind::leaf || n >= shell.ws.rects.size() || node.leaf.tabs.empty()) {
            continue;
        }
        row r{ "", { 0, 0 }, { shell.ws.rects[n].w, shell.ws.rects[n].h }, ImVec2(0, 0) };
        for (const rv_editor_pane_id id : node.leaf.tabs) {
            const rv_editor_size m = minimum_of(shell, id);
            r.min = { std::max(r.min.w, m.w), std::max(r.min.h, m.h) };
            r.kinds += std::string(r.kinds.empty() ? "" : "+") + rv_editor_pane_title(shell.ws.panes.panes[id].kind);
        }
        const auto it = d.body.find(node.leaf.tabs[node.leaf.active]);
        if (it == d.body.end()) {
            continue;
        }
        r.body = it->second;
        if (r.body.x + 0.5f < static_cast<float>(r.min.w) || r.body.y + 0.5f < static_cast<float>(r.min.h)) {
            ++bad;
            std::fprintf(stderr,
                "  %s: %s body %.0fx%.0f under its minimum %dx%d\n",
                name,
                r.kinds.c_str(),
                r.body.x,
                r.body.y,
                r.min.w,
                r.min.h);
        }
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(), [](const row &a, const row &b) {
        return a.rect.w * a.rect.h > b.rect.w * b.rect.h;
    });
    for (const row &r : rows) {
        std::fprintf(stderr,
            "  %s: leaf %s rect %dx%d body %.0fx%.0f min %dx%d\n",
            name,
            r.kinds.c_str(),
            r.rect.w,
            r.rect.h,
            r.body.x,
            r.body.y,
            r.min.w,
            r.min.h);
    }
    return bad;
}

void make(rv_editor_shell &shell, rv_editor_layout_preset preset)
{
    shell.ws.panes = {};
    rv_editor_layout_preset_make(preset, shell.ws.panes, shell.ws.layout);
    shell.ws.focused_leaf = shell.ws.layout.root;
}

void load_texts()
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
}

} // namespace

const rv_editor_size fit_windows[] = { { 1280, 720 }, { 1920, 1080 } };

// R21, AC22: the tree's minimum is within the work area of 1280x720 and 1920x1080, and every leaf is at least the minimum
// of its tabs.
RV_EDITOR_TEST(rv_editor_test_layout_fit_default_layouts_fit_the_window)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_size &window : fit_windows) {
        for (const preset_case &c : presets) {
            rv_editor_shell shell;
            make(shell, c.preset);
            const drawn d = draw(gui, shell, window.w, window.h);
            std::fprintf(stderr,
                "%s %dx%d: tree minimum %dx%d in a work area %dx%d (over by %dx%d)\n",
                c.name,
                window.w,
                window.h,
                d.tree.w,
                d.tree.h,
                d.area.w,
                d.area.h,
                d.tree.w - d.area.w,
                d.tree.h - d.area.h);
            RV_EDITOR_CHECK(d.tree.w <= d.area.w);
            RV_EDITOR_CHECK(d.tree.h <= d.area.h);
            RV_EDITOR_CHECK(leaves_under_minimum(shell, d, c.name) == 0);
        }
    }
}

// AC22: the work area does not scroll for the starting layouts at 1280x720 and 1920x1080.
RV_EDITOR_TEST(rv_editor_test_layout_fit_default_layouts_do_not_scroll)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const rv_editor_size &window : fit_windows) {
        for (const preset_case &c : presets) {
            rv_editor_shell shell;
            make(shell, c.preset);
            const drawn d = draw(gui, shell, window.w, window.h);
            RV_EDITOR_CHECK(d.found_scroll);
            if (d.scroll_max.x > 0.5f || d.scroll_max.y > 0.5f) {
                std::fprintf(stderr,
                    "  %s %dx%d: the work area scrolls by %.0f x %.0f px\n",
                    c.name,
                    window.w,
                    window.h,
                    d.scroll_max.x,
                    d.scroll_max.y);
            }
            RV_EDITOR_CHECK(d.scroll_max.x <= 0.5f && d.scroll_max.y <= 0.5f);
        }
    }
}

// AC26: a saved tree whose Run Config tile is a sliver (a ratio of 0.02) opens without a crash, with the tile widened to
// its minimum by the existing mechanism.
RV_EDITOR_TEST(rv_editor_test_layout_fit_saved_tile_under_minimum_is_widened)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    rv_editor_pane_registry panes;
    rv_editor_layout layout;
    rv_editor_layout_preset_make(rv_editor_layout_preset::code, panes, layout);
    const rv_editor_pane_id run = rv_editor_pane_add(panes, rv_editor_pane_kind::run_config);
    const uint32_t leaf = rv_editor_tile_insert(layout, rv_editor_tile_find(layout, 0), run, rv_editor_tile_dock::right);
    RV_EDITOR_CHECK(leaf != rv_editor_tile_none);
    for (uint32_t n = 0; n < layout.nodes.size(); ++n) {
        const auto &node = layout.nodes[n];
        if (node.kind == rv_editor_tile_kind::split && (node.split.first == leaf || node.split.second == leaf)) {
            (void)rv_editor_tile_set_ratio(layout, n, node.split.first == leaf ? 0.02f : 0.98f);
        }
    }
    std::string text;
    std::string error;
    RV_EDITOR_CHECK(rv_editor_layout_write(panes, layout, text, error) == RV_OK);
    rv_editor_shell shell;
    RV_EDITOR_CHECK(rv_editor_layout_read(text, shell.ws.panes, shell.ws.layout, error) == RV_OK);
    shell.ws.focused_leaf = shell.ws.layout.root;
    // A window big enough for the tree: only the sliver is under its minimum.
    const drawn d = draw(gui, shell, 1920, 1080);
    RV_EDITOR_CHECK(leaves_under_minimum(shell, d, "saved") == 0);
    rv_editor_pane_id run_id = rv_editor_tile_none;
    for (size_t i = 0; i < shell.ws.panes.panes.size(); ++i) {
        if (shell.ws.panes.panes[i].kind == rv_editor_pane_kind::run_config) {
            run_id = static_cast<rv_editor_pane_id>(i);
        }
    }
    const uint32_t run_leaf = rv_editor_tile_find(shell.ws.layout, run_id);
    RV_EDITOR_CHECK(run_leaf != rv_editor_tile_none);
    if (run_leaf != rv_editor_tile_none) {
        const rv_editor_size m = minimum_of(shell, run_id);
        RV_EDITOR_CHECK(shell.ws.rects[run_leaf].w >= m.w && shell.ws.rects[run_leaf].h >= m.h);
    }
}

// R21: a window under the tree's minimum scrolls the work area, and the leaves still keep their minimums.
RV_EDITOR_TEST(rv_editor_test_layout_fit_small_window_scrolls)
{
    load_texts();
    rv_editor_test_imgui gui(true);
    for (const preset_case &c : presets) {
        rv_editor_shell shell;
        make(shell, c.preset);
        const drawn d = draw(gui, shell, 600, 400);
        RV_EDITOR_CHECK(d.tree.w > 600 || d.tree.h > 400); // the window is under the sum of the minimums
        RV_EDITOR_CHECK(d.found_scroll);
        RV_EDITOR_CHECK(d.scroll_max.x > 0.5f || d.scroll_max.y > 0.5f);
        RV_EDITOR_CHECK(leaves_under_minimum(shell, d, c.name) == 0);
    }
}
