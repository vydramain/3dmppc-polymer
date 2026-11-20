// The Debug starting layout: no Session Toolchest, Runtime Controls across the top, the Game as wide as its picture.

#include <algorithm>

#include "app/rv_editor_shell.hpp"
#include "layout/rv_editor_tile.hpp"
#include "panes/rv_editor_game_fit.hpp"
#include "rv_editor_test.hpp"

using namespace rv_editor;

namespace
{

// The pane kinds of leaf `node`, in tab order.
std::vector<rv_editor_pane_kind> kinds_of(const rv_editor_pane_registry &panes, const rv_editor_layout &layout, uint32_t node)
{
    std::vector<rv_editor_pane_kind> kinds;
    for (const rv_editor_pane_id id : layout.nodes[node].leaf.tabs) {
        kinds.push_back(panes.panes[id].kind);
    }
    return kinds;
}

bool leaf_has(const rv_editor_pane_registry &panes, const rv_editor_layout &layout, uint32_t node, rv_editor_pane_kind kind)
{
    if (layout.nodes[node].kind != rv_editor_tile_kind::leaf) {
        return false;
    }
    const auto kinds = kinds_of(panes, layout, node);
    return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}

bool is_split(const rv_editor_layout &layout, uint32_t node, rv_editor_axis axis)
{
    return layout.nodes[node].kind == rv_editor_tile_kind::split && layout.nodes[node].split.axis == axis;
}

constexpr size_t debug_slot = 2; // rv_editor_workspaces order: code, scene, debug, burn

// Stand-ins for what the view measures: bar, leaf chrome, the Game's shelf and status line, the strip height.
constexpr int32_t bar_px = 4;
constexpr rv_editor_size chrome_px{ 12, 36 };
constexpr int32_t tabs_px = 20;
constexpr int32_t game_extra_h = 52;
constexpr int32_t strip_h = 40;

// Frames of the shell's own loop the layout gets to settle in (the shell allows eight fit tries).
constexpr int settle_frames = 40;

// Places Debug in a window of `w` x `h` the way the shell does, frame by frame, with the Game's area measured from
// its rectangle; returns the width left beside a 320x240 picture in Fit, or -1 when the tree has no Game.
int game_side_bands(int32_t w, int32_t h, int32_t &controls_w)
{
    rv_editor_shell shell;
    rv_editor_layout_preset_make(rv_editor_layout_preset::debug, shell.ws.panes, shell.ws.layout);
    shell.active = rv_editor_layout_preset::debug;
    shell.game_fit[debug_slot] = rv_editor_preset_fits_game(rv_editor_layout_preset::debug);
    shell.layout_untouched[debug_slot] = true;
    shell.app.project.screen_w = 320;
    shell.app.project.screen_h = 240;

    const rv_editor_tile_metrics m{ bar_px, chrome_px, tabs_px };
    std::vector<rv_editor_size> pane_min(shell.ws.panes.panes.size(), rv_editor_size{ 160, 80 });
    rv_editor_pane_id game = rv_editor_tile_none;
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
    if (game == rv_editor_tile_none) {
        return -1;
    }

    rv_editor_size area{ 0, 0 };
    for (int frame = 0; frame < settle_frames; ++frame) {
        const rv_editor_size need = rv_editor_tile_min_size(shell.ws.layout, shell.ws.layout.root, m, pane_min);
        const rv_editor_rect window{ 0, 0, std::max(w, need.w), std::max(h, need.h) };
        std::vector<rv_editor_rect> rects(shell.ws.layout.nodes.size());
        for (const auto &place : rv_editor_layout_place(shell.ws.layout, window, m, pane_min)) {
            rects[place.node] = place.rect;
        }
        shell.ws.rects = rects;
        const rv_editor_rect g = rects[rv_editor_tile_find(shell.ws.layout, game)];
        area = { g.w - chrome_px.w, g.h - chrome_px.h - game_extra_h };
        shell.ui.game_area = area;
        // Pane 0 is the Runtime Controls: the preset starts from it.
        controls_w = rects[rv_editor_tile_find(shell.ws.layout, 0)].w;
        rv_editor_shell_frame_start(shell);
    }
    const rv_editor_game_view view =
        rv_editor_game_place(320, 240, static_cast<float>(area.w), static_cast<float>(area.h), rv_editor_game_scale::fit);
    return static_cast<int>(static_cast<float>(area.w) - view.w);
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_debug_layout_has_no_toolchest)
{
    rv_editor_pane_registry panes;
    rv_editor_layout layout;
    rv_editor_layout_preset_make(rv_editor_layout_preset::debug, panes, layout);
    for (uint32_t i = 0; i < layout.nodes.size(); ++i) {
        if (layout.nodes[i].kind == rv_editor_tile_kind::leaf) {
            RV_EDITOR_CHECK(!leaf_has(panes, layout, i, rv_editor_pane_kind::toolchest));
        }
    }
}

RV_EDITOR_TEST(rv_editor_test_debug_layout_controls_across_the_top)
{
    rv_editor_pane_registry panes;
    rv_editor_layout layout;
    rv_editor_layout_preset_make(rv_editor_layout_preset::debug, panes, layout);
    RV_EDITOR_CHECK(is_split(layout, layout.root, rv_editor_axis::y));
    const rv_editor_tile_split &root = layout.nodes[layout.root].split;
    // The top child is the controls leaf alone: a strip as wide as the window.
    RV_EDITOR_CHECK(layout.nodes[root.first].kind == rv_editor_tile_kind::leaf);
    RV_EDITOR_CHECK(kinds_of(panes, layout, root.first) == std::vector<rv_editor_pane_kind>{ rv_editor_pane_kind::controls });
}

RV_EDITOR_TEST(rv_editor_test_debug_layout_game_beside_session_over_log)
{
    rv_editor_pane_registry panes;
    rv_editor_layout layout;
    rv_editor_layout_preset_make(rv_editor_layout_preset::debug, panes, layout);
    RV_EDITOR_CHECK(is_split(layout, layout.root, rv_editor_axis::y));
    const uint32_t rest = layout.nodes[layout.root].split.second;
    // Below the strip: the Game beside the session column, then the log along the bottom.
    RV_EDITOR_CHECK(is_split(layout, rest, rv_editor_axis::y));
    if (!is_split(layout, rest, rv_editor_axis::y)) {
        return;
    }
    const uint32_t middle = layout.nodes[rest].split.first;
    const uint32_t bottom = layout.nodes[rest].split.second;
    RV_EDITOR_CHECK(is_split(layout, middle, rv_editor_axis::x));
    if (is_split(layout, middle, rv_editor_axis::x)) {
        const rv_editor_tile_split &row = layout.nodes[middle].split;
        RV_EDITOR_CHECK(kinds_of(panes, layout, row.first) == std::vector<rv_editor_pane_kind>{ rv_editor_pane_kind::game });
        RV_EDITOR_CHECK(leaf_has(panes, layout, row.second, rv_editor_pane_kind::session));
        RV_EDITOR_CHECK(leaf_has(panes, layout, row.second, rv_editor_pane_kind::observe));
        RV_EDITOR_CHECK(leaf_has(panes, layout, row.second, rv_editor_pane_kind::test_case));
    }
    RV_EDITOR_CHECK(leaf_has(panes, layout, bottom, rv_editor_pane_kind::runtime_log));
    RV_EDITOR_CHECK(leaf_has(panes, layout, bottom, rv_editor_pane_kind::findings));
}

RV_EDITOR_TEST(rv_editor_test_preset_fits_game_for_debug_and_burn)
{
    RV_EDITOR_CHECK(rv_editor_preset_fits_game(rv_editor_layout_preset::debug));
    RV_EDITOR_CHECK(rv_editor_preset_fits_game(rv_editor_layout_preset::burn));
}

RV_EDITOR_TEST(rv_editor_test_debug_layout_game_has_no_wide_side_bands)
{
    const rv_editor_size windows[] = { { 1280, 720 }, { 1280, 480 } };
    for (const rv_editor_size &win : windows) {
        int32_t controls_w = 0;
        const int bands = game_side_bands(win.w, win.h, controls_w);
        RV_EDITOR_CHECK(bands >= 0 && bands <= 16);
        RV_EDITOR_CHECK(controls_w == win.w);
    }
}
