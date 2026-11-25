// Helpers of the layout mockup tests (rv_editor_test_layout_mockup.cpp and rv_editor_test_layout_mockup_loaded.cpp): a preset
// drawn in a window through the shell's own loop until it settles, and the checks on its picture area and tiles.

#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "font/rv_editor_font.hpp"
#include "imgui_internal.h"
#include "layout/rv_editor_tile.hpp"
#include "rv_editor_test_imgui.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor_test
{

namespace layout_mockup
{

using namespace rv_editor;

constexpr rv_editor_theme theme = rv_editor_theme_olive;
constexpr float status_paddings = 2.0f; // main.cpp window_padding_sides
constexpr int settle_frames = 10;       // the fit has its frames
constexpr int hold_frames = 6;          // and then it holds
constexpr int tol = 1;                  // pixels of rounding

using kind = rv_editor_pane_kind;

const rv_editor_size windows[] = { { 1280, 720 }, { 1920, 1080 } };

struct view {
    rv_editor_shell shell;
    rv_editor_rect work{ 0, 0, 0, 0 }; // the work area the workspace was given
    rv_editor_rect root{ 0, 0, 0, 0 }; // the placed tree: the work area, or larger when the minimums need it
    rv_editor_size picture{ 0, 0 };    // the Game's picture area as the Game pane measured it
    rv_editor_size need{ 0, 0 };       // the tree's minimum
    int32_t bar = 0;
    const char *name = "";
    std::string where;

    // The leaf holding the first pane of `k`.
    uint32_t leaf(kind k) const
    {
        for (size_t i = 0; i < shell.ws.panes.panes.size(); ++i) {
            if (shell.ws.panes.panes[i].kind == k) {
                return rv_editor_tile_find(shell.ws.layout, static_cast<rv_editor_pane_id>(i));
            }
        }
        return rv_editor_tile_none;
    }
    rv_editor_rect rect(kind k) const
    {
        const uint32_t n = leaf(k);
        return n < shell.ws.rects.size() ? shell.ws.rects[n] : rv_editor_rect{ 0, 0, 0, 0 };
    }
    // True when leaf `n` holds exactly `kinds`, in this tab order.
    bool tabs_are(kind k, const std::vector<kind> &kinds) const
    {
        const uint32_t n = leaf(k);
        if (n == rv_editor_tile_none || shell.ws.layout.nodes[n].leaf.tabs.size() != kinds.size()) {
            return false;
        }
        for (size_t i = 0; i < kinds.size(); ++i) {
            if (shell.ws.panes.panes[shell.ws.layout.nodes[n].leaf.tabs[i]].kind != kinds[i]) {
                return false;
            }
        }
        return true;
    }
};

// A check that says what failed: the preset and window, and the statement.
inline bool say(const view &v, bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "  %s: %s\n", v.where.c_str(), what);
    }
    return ok;
}

inline void dump(const view &v)
{
    for (uint32_t n = 0; n < v.shell.ws.layout.nodes.size(); ++n) {
        const auto &node = v.shell.ws.layout.nodes[n];
        if (node.kind != rv_editor_tile_kind::leaf || node.leaf.tabs.empty() || n >= v.shell.ws.rects.size()) {
            continue;
        }
        const rv_editor_rect &r = v.shell.ws.rects[n];
        std::fprintf(stderr,
            "    %s %dx%d at (%d,%d)\n",
            rv_editor_pane_title(v.shell.ws.panes.panes[node.leaf.tabs[0]].kind),
            r.w,
            r.h,
            r.x,
            r.y);
    }
    std::fprintf(stderr,
        "    work area %dx%d, tree minimum %dx%d, picture %dx%d\n",
        v.work.w,
        v.work.h,
        v.need.w,
        v.need.h,
        v.picture.w,
        v.picture.h);
}

// Frames in a `win` window; false if the picture moved over the last `hold_frames`.
inline bool run(view &v, rv_editor_test_imgui &gui, const rv_editor_size &win)
{
    rv_editor_shell &shell = v.shell;
    rv_editor_size held{ 0, 0 };
    bool steady = true;
    for (int frame = 0; frame < settle_frames + hold_frames; ++frame) {
        gui.frame(
            [&] {
                rv_editor_theme_apply(theme, ImGui::GetStyle());
                ImGui::GetStyle().FontSizeBase = rv_editor_font_ui()->LegacySize;
                rv_editor_shell_frame_start(shell);
                shell.ws.minimums.clear();
                for (size_t i = 0; i < shell.ws.panes.panes.size(); ++i) {
                    rv_editor_shell_set_minimum(shell,
                        static_cast<rv_editor_pane_id>(i),
                        shell.ws.panes.panes[i].kind,
                        { 0, 0 });
                }
                const float frame_h = ImGui::GetFrameHeight();
                const float bars = frame_h + frame_h + status_paddings * ImGui::GetStyle().WindowPadding.y;
                v.work = { 0, static_cast<int>(frame_h), win.w, static_cast<int>(static_cast<float>(win.h) - bars) };
                std::string error;
                rv_editor_workspace_draw(
                    shell.ws,
                    theme,
                    [](void *context, rv_editor_pane_id pane, rv_editor_pane_kind k, const rv_editor_theme &t) {
                        // Only the Game is drawn: it is the pane that measures what the fit needs.
                        if (k == kind::game) {
                            rv_editor_shell_pane(context, pane, k, t);
                        }
                    },
                    nullptr,
                    &shell,
                    v.work,
                    error);
                rv_editor_shell_ui_apply(shell);
            },
            static_cast<float>(win.w),
            static_cast<float>(win.h));
        v.picture = shell.ui.game_area;
        const rv_editor_rect &root = shell.ws.rects[shell.ws.layout.root];
        v.need = { root.w, root.h };
        v.root = root;
        v.bar = shell.ws.tile_metrics.bar;
        if (frame == settle_frames - 1) {
            held = v.picture;
        } else if (frame >= settle_frames && (v.picture.w != held.w || v.picture.h != held.h)) {
            steady = false;
        }
    }
    return steady;
}

inline const char *preset_name(rv_editor_layout_preset preset)
{
    switch (preset) {
    case rv_editor_layout_preset::code:
        return "Code";
    case rv_editor_layout_preset::scene:
        return "Scene";
    case rv_editor_layout_preset::debug:
        return "Debug";
    case rv_editor_layout_preset::burn:
        return "Burn";
    }
    return "";
}

// The Game's picture area is exactly the target in both windows: 400x300 / 640x480, in Scene 320x240 / 480x360.
inline bool picture_is(const view &v, bool big, bool scene)
{
    const rv_editor_size want = big ? (scene ? rv_editor_size{ 480, 360 } : rv_editor_size{ 640, 480 }) :
                                      (scene ? rv_editor_size{ 320, 240 } : rv_editor_size{ 400, 300 });
    char text[96];
    std::snprintf(text, sizeof text, "the Game's picture area is %dx%d, not %dx%d", v.picture.w, v.picture.h, want.w, want.h);
    return say(v, v.picture.w == want.w && v.picture.h == want.h, text);
}

} // namespace layout_mockup

} // namespace rv_editor_test
