// The Debug starting layout: no Session Toolchest, Runtime Controls over the Game column, the Game as wide as its picture.

#include <algorithm>

#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "layout/rv_editor_tile.hpp"
#include "panes/rv_editor_game_fit.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

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
int game_side_bands(int32_t w, int32_t h, int32_t &controls_w, int32_t &game_w)
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
        game_w = g.w;
        rv_editor_shell_frame_start(shell);
    }
    const rv_editor_game_view view =
        rv_editor_game_place(320, 240, static_cast<float>(area.w), static_cast<float>(area.h), rv_editor_game_scale::fit);
    return static_cast<int>(static_cast<float>(area.w) - view.w);
}

} // namespace

// S7.2: the Mark, Capture and Report tiles left Runtime Controls, so Debug starts with a Toolchest that holds them.
RV_EDITOR_TEST(rv_editor_test_debug_layout_has_toolchest)
{
    rv_editor_pane_registry panes;
    rv_editor_layout layout;
    rv_editor_layout_preset_make(rv_editor_layout_preset::debug, panes, layout);
    bool found = false;
    for (uint32_t i = 0; i < layout.nodes.size(); ++i) {
        found = found || leaf_has(panes, layout, i, rv_editor_pane_kind::toolchest);
    }
    RV_EDITOR_CHECK(found);
}

namespace
{

// The Toolchest's leaf rectangle in a `w` x `h` window, from the Debug starting layout's initial placement (the
// shell's game fit is not applied); false if the layout has no Toolchest leaf.
bool toolchest_rect(rv_editor_shell &shell, int32_t w, int32_t h, rv_editor_rect &out)
{
    rv_editor_layout_preset_make(rv_editor_layout_preset::debug, shell.ws.panes, shell.ws.layout);
    std::vector<rv_editor_size> pane_min(shell.ws.panes.panes.size(), rv_editor_size{ 0, 0 });
    for (size_t i = 0; i < pane_min.size(); ++i) {
        rv_editor_shell_set_minimum(shell, static_cast<rv_editor_pane_id>(i), shell.ws.panes.panes[i].kind, { 0, 0 });
        const auto it = shell.ws.minimums.find(static_cast<rv_editor_pane_id>(i));
        if (it != shell.ws.minimums.end()) {
            pane_min[i] = it->second;
        }
    }
    const rv_editor_tile_metrics m{ bar_px, chrome_px, tabs_px };
    for (const auto &place : rv_editor_layout_place(shell.ws.layout, rv_editor_rect{ 0, 0, w, h }, m, pane_min)) {
        if (leaf_has(shell.ws.panes, shell.ws.layout, place.node, rv_editor_pane_kind::toolchest)) {
            out = place.rect;
            return true;
        }
    }
    return false;
}

// The Toolchest's mark row, as rv_editor_text keys (the small tiles carry the name only as an id).
const char *const toolchest_mark_keys[] = { "pane_toolchest.mark_button",
    "pane_toolchest.capture_button",
    "pane_toolchest.report_button" };

bool drawn_label(const rv_editor_probe_record &r, const std::string &name)
{
    for (const auto &e : r.entries) {
        if (e.label == name || e.label == "##" + name) {
            return true;
        }
    }
    return false;
}

} // namespace

// S8 at 1280x720: the Debug Toolchest's leaf holds Restart; Mark, Capture, Report and the note field are in Session's Notes.
RV_EDITOR_TEST(rv_editor_test_debug_toolchest_draws_restart_without_the_marks_at_1280x720)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    rv_editor_shell shell;
    shell.app.preset = rv_editor_layout_preset::debug;
    rv_editor_rect tile{ 0, 0, 0, 0 };
    RV_EDITOR_CHECK(toolchest_rect(shell, 1280, 720, tile));
    if (tile.w <= 0 || tile.h <= 0) {
        return;
    }
    const auto body = [&](rv_editor_probe_record &) {
        rv_editor_shell_pane(&shell, 1, rv_editor_pane_kind::toolchest, stable_tiles::theme);
    };
    rv_editor_probe_draw(gui, static_cast<float>(tile.w), body, static_cast<float>(tile.h));
    const auto r = rv_editor_probe_draw(gui, static_cast<float>(tile.w), body, static_cast<float>(tile.h));
    const std::string restart = rv_editor_text("pane_toolchest.restart_button");
    if (!drawn_label(r, restart)) {
        std::fprintf(stderr, "  '%s' is not drawn in the %dx%d Toolchest\n", restart.c_str(), tile.w, tile.h);
    }
    RV_EDITOR_CHECK(drawn_label(r, restart));
    for (const char *key : toolchest_mark_keys) {
        const std::string name = rv_editor_text(key);
        if (drawn_label(r, name)) {
            std::fprintf(stderr, "  '%s' is drawn in the Toolchest\n", name.c_str());
        }
        RV_EDITOR_CHECK(!drawn_label(r, name));
    }
    RV_EDITOR_CHECK(!drawn_label(r, "mark_note"));
}

// Root: the upper row over the log; the row: the Game column beside the session tabs; the column: controls over Game.
RV_EDITOR_TEST(rv_editor_test_debug_layout_controls_across_the_top)
{
    rv_editor_pane_registry panes;
    rv_editor_layout layout;
    rv_editor_layout_preset_make(rv_editor_layout_preset::debug, panes, layout);
    RV_EDITOR_CHECK(is_split(layout, layout.root, rv_editor_axis::y));
    if (!is_split(layout, layout.root, rv_editor_axis::y)) {
        return;
    }
    const uint32_t middle = layout.nodes[layout.root].split.first;
    RV_EDITOR_CHECK(is_split(layout, middle, rv_editor_axis::x));
    if (!is_split(layout, middle, rv_editor_axis::x)) {
        return;
    }
    const uint32_t column = layout.nodes[middle].split.first;
    RV_EDITOR_CHECK(is_split(layout, column, rv_editor_axis::y));
    if (!is_split(layout, column, rv_editor_axis::y)) {
        return;
    }
    const rv_editor_tile_split &strip = layout.nodes[column].split;
    // The strip is the controls leaf alone, above the Game leaf alone: one column, not a window-wide bar.
    RV_EDITOR_CHECK(kinds_of(panes, layout, strip.first) == std::vector<rv_editor_pane_kind>{ rv_editor_pane_kind::controls });
    RV_EDITOR_CHECK(kinds_of(panes, layout, strip.second) == std::vector<rv_editor_pane_kind>{ rv_editor_pane_kind::game });
}

RV_EDITOR_TEST(rv_editor_test_debug_layout_game_beside_session_over_log)
{
    rv_editor_pane_registry panes;
    rv_editor_layout layout;
    rv_editor_layout_preset_make(rv_editor_layout_preset::debug, panes, layout);
    RV_EDITOR_CHECK(is_split(layout, layout.root, rv_editor_axis::y));
    if (!is_split(layout, layout.root, rv_editor_axis::y)) {
        return;
    }
    const uint32_t middle = layout.nodes[layout.root].split.first;
    const uint32_t bottom = layout.nodes[layout.root].split.second;
    // The Game column (controls over Game) beside the session tabs, then the log tabs along the bottom.
    RV_EDITOR_CHECK(is_split(layout, middle, rv_editor_axis::x));
    if (is_split(layout, middle, rv_editor_axis::x)) {
        const rv_editor_tile_split &row = layout.nodes[middle].split;
        RV_EDITOR_CHECK(is_split(layout, row.first, rv_editor_axis::y));
        RV_EDITOR_CHECK(leaf_has(panes, layout, row.second, rv_editor_pane_kind::session));
        RV_EDITOR_CHECK(leaf_has(panes, layout, row.second, rv_editor_pane_kind::observe));
        RV_EDITOR_CHECK(leaf_has(panes, layout, row.second, rv_editor_pane_kind::test_case));
    }
    RV_EDITOR_CHECK(leaf_has(panes, layout, bottom, rv_editor_pane_kind::runtime_log));
    RV_EDITOR_CHECK(leaf_has(panes, layout, bottom, rv_editor_pane_kind::findings));
    RV_EDITOR_CHECK(leaf_has(panes, layout, bottom, rv_editor_pane_kind::toolchest));
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
        int32_t game_w = 0;
        const int bands = game_side_bands(win.w, win.h, controls_w, game_w);
        RV_EDITOR_CHECK(bands >= 0 && bands <= 16);
        // The strip is as wide as the Game column, not as the window.
        RV_EDITOR_CHECK(controls_w == game_w);
        RV_EDITOR_CHECK(controls_w < win.w);
    }
}
