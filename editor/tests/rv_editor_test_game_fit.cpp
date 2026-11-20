// The Game tile's fit: in every starting layout its picture area takes the project's screen aspect exactly, in a
// step or two, stays there, follows a window resize, and leaves a tree the user dragged alone.

#include <algorithm>

#include <cstdio>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "layout/rv_editor_tile.hpp"
#include "panes/rv_editor_game_fit.hpp"
#include "rv_editor_test.hpp"

using namespace rv_editor;

namespace
{

// Stand-ins for what the view measures (as in the Debug layout tests): bar, leaf chrome, tabs, the Game's shelf and
// status line, the strip height.
constexpr int32_t bar_px = 4;
constexpr rv_editor_size chrome_px{ 12, 36 };
constexpr int32_t tabs_px = 20;
constexpr int32_t game_extra_h = 52;
constexpr int32_t strip_h = 40;

// A 4:3 area at 320x240: width x 3 == height x 4.
constexpr int exact_frames = 2; // frames the fit gets to reach the exact area
constexpr int hold_frames = 12; // frames it must then keep it

const rv_editor_layout_preset presets[] = { rv_editor_layout_preset::code,
    rv_editor_layout_preset::scene,
    rv_editor_layout_preset::debug,
    rv_editor_layout_preset::burn };
const rv_editor_size windows[] = { { 1280, 720 }, { 1920, 1080 } };

const char *preset_name(rv_editor_layout_preset preset)
{
    return rv_editor_layout_preset_name(preset);
}

// The shell's own loop in a window: place, measure the Game's area, frame start.
struct fit_loop {
    rv_editor_shell shell;
    std::vector<rv_editor_size> pane_min;
    rv_editor_pane_id game = rv_editor_tile_none;
    rv_editor_size area{ 0, 0 };

    explicit fit_loop(rv_editor_layout_preset preset)
    {
        rv_editor_layout_preset_make(preset, shell.ws.panes, shell.ws.layout);
        shell.active = preset;
        const size_t slot = static_cast<size_t>(preset); // the order of rv_editor_workspaces
        shell.game_fit[slot] = rv_editor_preset_fits_game(preset);
        shell.layout_untouched[slot] = true;
        shell.app.project.screen_w = 320;
        shell.app.project.screen_h = 240;
        pane_min.assign(shell.ws.panes.panes.size(), rv_editor_size{ 160, 80 });
        for (size_t i = 0; i < pane_min.size(); ++i) {
            const rv_editor_pane_kind kind = shell.ws.panes.panes[i].kind;
            if (kind == rv_editor_pane_kind::game) {
                game = static_cast<rv_editor_pane_id>(i);
                pane_min[i] = { 320, 240 + game_extra_h };
            } else if (kind == rv_editor_pane_kind::controls || kind == rv_editor_pane_kind::toolchest) {
                pane_min[i] = { 160, strip_h };
            }
            shell.ws.minimums[static_cast<rv_editor_pane_id>(i)] = pane_min[i];
        }
    }

    // One frame in a `w` x `h` window; `area` is the Game's picture area as drawn in it.
    void frame(int32_t w, int32_t h)
    {
        const rv_editor_tile_metrics m{ bar_px, chrome_px, tabs_px };
        rv_editor_layout &layout = shell.ws.layout;
        const rv_editor_size need = rv_editor_tile_min_size(layout, layout.root, m, pane_min);
        const rv_editor_rect window{ 0, 0, std::max(w, need.w), std::max(h, need.h) };
        std::vector<rv_editor_rect> rects(layout.nodes.size());
        for (const auto &place : rv_editor_layout_place(layout, window, m, pane_min)) {
            rects[place.node] = place.rect;
        }
        shell.ws.rects = rects;
        const rv_editor_rect g = rects[rv_editor_tile_find(layout, game)];
        area = { g.w - chrome_px.w, g.h - chrome_px.h - game_extra_h };
        shell.ui.game_area = area;
        rv_editor_shell_frame_start(shell);
    }

    bool exact() const
    {
        return area.w > 0 && area.h > 0 && area.w * 3 == area.h * 4;
    }
};

bool same(const rv_editor_size &a, const rv_editor_size &b)
{
    return a.w == b.w && a.h == b.h;
}

// Runs frames in `win` until the fit has had `exact_frames`, checks the area is exact, then that it holds; returns
// the area, and false if it was not exact or moved.
bool settle(fit_loop &loop, const rv_editor_size &win, const char *what)
{
    bool ok = true;
    for (int frame = 0; frame < exact_frames; ++frame) {
        loop.frame(win.w, win.h);
    }
    const rv_editor_size reached = loop.area;
    if (!loop.exact()) {
        std::fprintf(stderr, "  %s: area %dx%d is not 4:3 after %d frames\n", what, reached.w, reached.h, exact_frames);
        ok = false;
    }
    for (int frame = 0; frame < hold_frames; ++frame) {
        loop.frame(win.w, win.h);
        if (!same(loop.area, reached)) {
            std::fprintf(stderr,
                "  %s: area moved %dx%d -> %dx%d at frame %d after settling\n",
                what,
                reached.w,
                reached.h,
                loop.area.w,
                loop.area.h,
                exact_frames + frame);
            return false;
        }
    }
    return ok;
}

// A leaf of controls or the Toolchest only, or a split of such: the strips' heights follow their buttons, not the fit.
bool strip_side(const rv_editor_shell &shell, uint32_t node)
{
    const rv_editor_tile_node &n = shell.ws.layout.nodes[node];
    if (n.kind == rv_editor_tile_kind::split) {
        return strip_side(shell, n.split.first) && strip_side(shell, n.split.second);
    }
    for (const rv_editor_pane_id id : n.leaf.tabs) {
        const rv_editor_pane_kind kind = shell.ws.panes.panes[id].kind;
        if (kind != rv_editor_pane_kind::controls && kind != rv_editor_pane_kind::release_controls &&
            kind != rv_editor_pane_kind::toolchest) {
            return false;
        }
    }
    return !n.leaf.tabs.empty();
}

// The ratios of the splits that have no strip beside them.
std::vector<float> free_ratios(const rv_editor_shell &shell)
{
    std::vector<float> r;
    for (const auto &n : shell.ws.layout.nodes) {
        const bool free = n.kind == rv_editor_tile_kind::split && !strip_side(shell, n.split.first) &&
            !strip_side(shell, n.split.second);
        r.push_back(free ? n.split.ratio : 0.0f);
    }
    return r;
}

} // namespace

// Every starting layout, in 1280x720 and 1920x1080: the area is 4:3 exactly within two frames and then does not move.
RV_EDITOR_TEST(rv_editor_test_game_fit_exact_aspect_in_two_frames_and_holds)
{
    for (const rv_editor_layout_preset preset : presets) {
        for (const rv_editor_size &win : windows) {
            fit_loop loop(preset);
            char what[64];
            std::snprintf(what, sizeof what, "%s %dx%d", preset_name(preset), win.w, win.h);
            RV_EDITOR_CHECK(loop.game != rv_editor_tile_none);
            RV_EDITOR_CHECK(settle(loop, win, what));
        }
    }
}

// 1280x720 -> 1920x1080 -> 1280x720 in one shell: each size is exact within two frames, and coming back gives the
// area a fresh layout has at 1280x720.
RV_EDITOR_TEST(rv_editor_test_game_fit_follows_window_resize_and_returns)
{
    for (const rv_editor_layout_preset preset : presets) {
        fit_loop fresh(preset);
        const bool fresh_ok = settle(fresh, windows[0], preset_name(preset));
        RV_EDITOR_CHECK(fresh_ok);
        fit_loop loop(preset);
        RV_EDITOR_CHECK(settle(loop, windows[0], preset_name(preset)));
        RV_EDITOR_CHECK(settle(loop, windows[1], preset_name(preset)));
        RV_EDITOR_CHECK(settle(loop, windows[0], preset_name(preset)));
        if (!same(loop.area, fresh.area)) {
            std::fprintf(stderr,
                "  %s: back at 1280x720 the area is %dx%d, a fresh layout has %dx%d\n",
                preset_name(preset),
                loop.area.w,
                loop.area.h,
                fresh.area.w,
                fresh.area.h);
        }
        RV_EDITOR_CHECK(same(loop.area, fresh.area));
    }
}


// A dragged splitter is the user's: the fit leaves every ratio as the drag left it, through frames and a resize.
RV_EDITOR_TEST(rv_editor_test_game_fit_leaves_a_dragged_tree_alone)
{
    for (const rv_editor_layout_preset preset : presets) {
        fit_loop loop(preset);
        for (int frame = 0; frame < exact_frames + hold_frames; ++frame) {
            loop.frame(windows[0].w, windows[0].h);
        }
        // The drag: the nearest split above the Game with no strip beside it moves by a few percent, and the view says so.
        rv_editor_layout &layout = loop.shell.ws.layout;
        uint32_t split = layout.nodes[rv_editor_tile_find(layout, loop.game)].parent;
        while (split != rv_editor_tile_none &&
            (strip_side(loop.shell, layout.nodes[split].split.first) || strip_side(loop.shell, layout.nodes[split].split.second))) {
            split = layout.nodes[split].parent;
        }
        RV_EDITOR_CHECK(split != rv_editor_tile_none);
        if (split == rv_editor_tile_none) {
            continue;
        }
        float &ratio = layout.nodes[split].split.ratio;
        ratio += ratio > 0.5f ? -0.03f : 0.03f;
        loop.shell.ws.dragged = true;
        const std::vector<float> before = free_ratios(loop.shell);
        for (int frame = 0; frame < hold_frames; ++frame) {
            loop.frame(windows[0].w, windows[0].h);
        }
        for (int frame = 0; frame < hold_frames; ++frame) {
            loop.frame(windows[1].w, windows[1].h);
        }
        if (before != free_ratios(loop.shell)) {
            std::fprintf(stderr, "  %s: the fit changed a tree the user dragged\n", preset_name(preset));
        }
        RV_EDITOR_CHECK(before == free_ratios(loop.shell));
    }
}
