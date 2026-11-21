// S13.1b, R14, R15, R19, AC15, AC16: the minimums of Files, Assets and Inspector let the mockup's layouts be reached, measured
// on the real font through the workspace's own placement: a tile is the pane's minimum plus the chrome of a tile (and the tab
// row).
//  - Files at most 176 px wide (Code, 1280x720);
//  - Scene, 1280x720: the right column is, top to bottom, Toolchest, Runtime Controls, the Game (picture 320x240) and
//    Hierarchy, all one column wide (the Game tile for a 320 picture, no side bands); Assets (Assets, Files, Inspector Report;
//    no Console Output) and Inspector share what the column leaves, equally
//    (+-1), so each tile's minimum is at most half of it; the columns fit 1280 across and 672 down;
//  - every state of Files (the folder-delete card, the questions with a field) shows whole at 176 and of Assets at its share.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "layout/rv_editor_tile.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_s15.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "font/rv_editor_font.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;
using namespace rv_editor_test;
using kind = rv_editor_pane_kind;

namespace
{

constexpr int32_t files_code_1280 = 176;   // mockup, section 5: Code at 1280x720
constexpr int32_t right_column_1280 = 336; // the Game tile's width for a 320 px picture, Scene at 1280x720 (S13.1a)
constexpr int32_t scene_work_w = 1280;
constexpr int32_t scene_work_h = 672;      // 720 less the menu bar and the status bar
constexpr int32_t scene_game_area_h = 240; // the Game's picture area at 1280x720

struct measured {
    rv_editor_size tile{ 0, 0 }; // the least tile holding the kinds as tabs of one leaf
    rv_editor_tile_metrics metrics{ 0, { 0, 0 }, 0 };
};

// The least tile of a leaf whose tabs are `kinds`: what the workspace places with (header width, chrome, tab row), after one
// real draw of the shell's minimums.
measured tile_of(rv_editor_test_imgui &gui, const std::vector<kind> &kinds)
{
    measured m;
    gui.frame(
        [&] {
            rv_editor_theme_apply(rv_editor_theme_olive, ImGui::GetStyle());
            ImGui::GetStyle().FontSizeBase = rv_editor_font_ui()->LegacySize;
            rv_editor_shell shell;
            shell.ws.panes.panes.clear();
            rv_editor_tile_node leaf{ rv_editor_tile_kind::leaf, rv_editor_tile_none, {}, {} };
            for (const kind k : kinds) {
                leaf.leaf.tabs.push_back(rv_editor_pane_add(shell.ws.panes, k));
            }
            leaf.leaf.active = 0;
            shell.ws.layout.nodes = { leaf };
            shell.ws.layout.root = 0;
            shell.ws.layout.maximized_leaf = rv_editor_tile_none;
            shell.ws.focused_leaf = 0;
            rv_editor_shell_frame_start(shell);
            shell.ws.minimums.clear();
            for (size_t i = 0; i < shell.ws.panes.panes.size(); ++i) {
                rv_editor_shell_set_minimum(shell, static_cast<rv_editor_pane_id>(i), shell.ws.panes.panes[i].kind, { 0, 0 });
            }
            std::string error;
            rv_editor_workspace_draw(
                shell.ws,
                rv_editor_theme_olive,
                [](void *, rv_editor_pane_id, rv_editor_pane_kind, const rv_editor_theme &) {},
                nullptr,
                &shell,
                { 0, 0, 4000, 4000 },
                error);
            m.metrics = shell.ws.tile_metrics;
            m.tile = rv_editor_tile_min_size(shell.ws.layout, 0, shell.ws.tile_metrics, shell.ws.pane_min);
        },
        4000.0f,
        4000.0f);
    return m;
}

const std::vector<kind> assets_tile = { kind::assets, kind::files, kind::inspector_report };

// The height a Game tile needs for a picture area of `area_h`: what is around the picture (measured in a tall window, as the
// Game's own minimum test does) plus the tile's chrome and tab row; the status is drawn over the picture (S13.1a).
int32_t game_tile_h(rv_editor_test_imgui &gui, const measured &chrome, int32_t area_h)
{
    float around = 0.0f;
    rv_editor_shell shell;
    shell.app.project.open = true;
    const float height = 600.0f;
    rv_editor_probe_record record;
    rv_editor_probe_detail::sink sink(&record);
    rv_editor_item_sink_set(&sink);
    float pad = 0.0f;
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
    gui.frame(
        [&] {
            pad = ImGui::GetStyle().WindowPadding.y;
            rv_editor_shell_pane(&shell, 1, kind::game, stable_tiles::theme);
        },
        800.0f,
        height);
    ImGui::GetCurrentContext()->TestEngineHookItems = false;
    rv_editor_item_sink_set(nullptr);
    if (!record.entries.empty()) {
        const auto &picture = record.entries.back();
        around = (height - 2.0f * pad) - (picture.y1 - picture.y0);
    }
    return static_cast<int32_t>(around + 0.5f) + area_h + chrome.metrics.chrome.h + chrome.metrics.tabs;
}

// "" or the first violation of every state of `which` drawn in a pane `w` wide and `h` high.
std::string states_fit(rv_editor_test_imgui &gui, kind which, float w, float h)
{
    s15::fixture f(gui, which);
    const std::string open = rv_editor_text("panes.open_project_button");
    for (size_t i = 0; i < f.states.size(); ++i) {
        f.states[i]();
        const rv_editor_probe_record record = f.settled(w, h);
        rv_editor_probe_record kept; // the no-project button's "..." is the menu's own
        kept.content_height = record.content_height;
        for (const auto &e : record.entries) {
            if (s15::text_of(e) != open) {
                kept.entries.push_back(e);
            }
        }
        if (const std::string bad = rv_editor_fits_whole(kept, ImRect(0, 0, w, h)); !bad.empty()) {
            return "state " + std::to_string(i) + ": " + bad;
        }
    }
    return "";
}

// What Scene at 1280x720 gives Assets and Inspector: the window less the right column and the two bars, half each (+-1, so the
// least half is what each minimum must fit in). The right column is as wide as the widest of Toolchest, Runtime Controls and
// the Game tile for a 320 picture.
struct scene_row {
    measured assets, inspector, toolchest, controls;
    int32_t column = 0; // the right column's width
    int32_t share = 0;  // the least width Assets or Inspector gets
};

scene_row scene_widths(rv_editor_test_imgui &gui)
{
    scene_row r;
    r.assets = tile_of(gui, assets_tile);
    r.inspector = tile_of(gui, { kind::inspector });
    r.toolchest = tile_of(gui, { kind::toolchest });
    r.controls = tile_of(gui, { kind::controls });
    const int32_t bar = r.assets.metrics.bar;
    r.column = std::max({ r.toolchest.tile.w, r.controls.tile.w, right_column_1280 });
    r.share = (scene_work_w - r.column - 2 * bar) / 2;
    std::fprintf(stderr,
        "  Toolchest %d, Controls %d, Game %d: column %d; Assets and Inspector get %d each (%d for both)\n",
        r.toolchest.tile.w,
        r.controls.tile.w,
        right_column_1280,
        r.column,
        r.share,
        scene_work_w - r.column - 2 * bar);
    return r;
}

} // namespace

// R14, AC15: the Files tile (the pane's minimum and the chrome) is at most 176 px wide.
RV_EDITOR_TEST(rv_editor_test_layout_minimums_files_tile_is_at_most_176)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    const measured m = tile_of(gui, { kind::files });
    std::fprintf(stderr, "  Files tile %dx%d, chrome %dx%d\n", m.tile.w, m.tile.h, m.metrics.chrome.w, m.metrics.chrome.h);
    RV_EDITOR_CHECK(m.tile.w > 0 && m.tile.w <= files_code_1280);
}

// R15, AC16: the Assets tile with its three tabs and the Inspector
// tile each fit in their equal share of what the right column leaves.
RV_EDITOR_TEST(rv_editor_test_layout_minimums_assets_and_inspector_fit_their_share_of_the_row)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    for (const kind k : assets_tile) {
        const measured one = tile_of(gui, { k });
        std::fprintf(stderr, "  tab %s alone: tile %dx%d\n", rv_editor_pane_title(k), one.tile.w, one.tile.h);
    }
    const scene_row r = scene_widths(gui);
    std::fprintf(stderr,
        "  Assets tile %dx%d, Inspector tile %dx%d, share %d\n",
        r.assets.tile.w,
        r.assets.tile.h,
        r.inspector.tile.w,
        r.inspector.tile.h,
        r.share);
    RV_EDITOR_CHECK(r.assets.tile.w > 0 && r.assets.tile.w <= r.share);
    RV_EDITOR_CHECK(r.inspector.tile.w > 0 && r.inspector.tile.w <= r.share);
}

// R19, AC15: at the width a Files tile of 176 px leaves the pane, every state (including the folder-delete card and the
// questions with a field) shows whole; its text wraps instead of being cut.
RV_EDITOR_TEST(rv_editor_test_layout_minimums_every_files_state_fits_whole_in_a_176_tile)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    const measured m = tile_of(gui, { kind::files });
    const stable_tiles::minimum own = stable_tiles::pane_minimum(gui, kind::files);
    const float w = static_cast<float>(files_code_1280 - m.metrics.chrome.w);
    const std::string bad = states_fit(gui, kind::files, w, own.h);
    if (!bad.empty()) {
        std::fprintf(stderr,
            "  Files at %.0fx%.0f (the pane's minimum is %.0fx%.0f): %s\n",
            w,
            own.h,
            own.w,
            own.h,
            bad.c_str());
    }
    RV_EDITOR_CHECK(bad.empty());
}

// R19, AC16: the same for Assets at the width of its share of the row.
RV_EDITOR_TEST(rv_editor_test_layout_minimums_every_assets_state_fits_whole_in_its_share)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    const scene_row r = scene_widths(gui);
    const stable_tiles::minimum own = stable_tiles::pane_minimum(gui, kind::assets);
    const float w = static_cast<float>(r.share - r.assets.metrics.chrome.w);
    const std::string bad = states_fit(gui, kind::assets, w, own.h);
    if (!bad.empty()) {
        std::fprintf(stderr,
            "  Assets at %.0fx%.0f (the pane's minimum is %.0fx%.0f): %s\n",
            w,
            own.h,
            own.w,
            own.h,
            bad.c_str());
    }
    RV_EDITOR_CHECK(bad.empty());
}

// R15: Scene at 1280x720, across: Assets, Inspector and the right column with the two bars are within 1280 (the real tiles
// summed).
RV_EDITOR_TEST(rv_editor_test_layout_minimums_scene_columns_fit_1280_across)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    const scene_row r = scene_widths(gui);
    const int32_t need = r.assets.tile.w + r.inspector.tile.w + r.column + 2 * r.assets.metrics.bar;
    std::fprintf(stderr,
        "  Assets %d + Inspector %d + column %d + 2 bars of %d = %d of %d\n",
        r.assets.tile.w,
        r.inspector.tile.w,
        r.column,
        r.assets.metrics.bar,
        need,
        scene_work_w);
    RV_EDITOR_CHECK(need <= scene_work_w);
}

// R15: Scene at 1280x720, down the left: the Scene over the row of Assets and Inspector is within the 672 px work area.
RV_EDITOR_TEST(rv_editor_test_layout_minimums_scene_left_column_fits_672_down)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    const measured scene = tile_of(gui, { kind::scene });
    const measured assets = tile_of(gui, assets_tile);
    const measured inspector = tile_of(gui, { kind::inspector });
    const int32_t row = std::max(assets.tile.h, inspector.tile.h);
    const int32_t need = scene.tile.h + assets.metrics.bar + row;
    std::fprintf(stderr,
        "  Scene %d + bar %d + row (Assets %d, Inspector %d) = %d of %d\n",
        scene.tile.h,
        assets.metrics.bar,
        assets.tile.h,
        inspector.tile.h,
        need,
        scene_work_h);
    RV_EDITOR_CHECK(need <= scene_work_h);
}

// R15, AC16: Scene at 1280x720, down the right: Toolchest, Runtime Controls, the Game with a 320x240 picture area and Hierarchy
// at its minimum, with three bars, are within 672 px.
RV_EDITOR_TEST(rv_editor_test_layout_minimums_scene_right_column_fits_672_down)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    const measured toolchest = tile_of(gui, { kind::toolchest });
    const measured controls = tile_of(gui, { kind::controls });
    const measured hierarchy = tile_of(gui, { kind::hierarchy });
    const int32_t game = game_tile_h(gui, toolchest, scene_game_area_h);
    const int32_t need = toolchest.tile.h + controls.tile.h + game + hierarchy.tile.h + 3 * toolchest.metrics.bar;
    std::fprintf(stderr,
        "  Toolchest %d + Controls %d + Game %d + Hierarchy %d + 3 bars of %d = %d of %d\n",
        toolchest.tile.h,
        controls.tile.h,
        game,
        hierarchy.tile.h,
        toolchest.metrics.bar,
        need,
        scene_work_h);
    RV_EDITOR_CHECK(need <= scene_work_h);
}

// R15, AC16: the right column of Scene is the Game tile's width for a 320 picture: the Toolchest and Runtime Controls minimums
// are not wider, so the picture fills the tile without side bands.
RV_EDITOR_TEST(rv_editor_test_layout_minimums_scene_right_column_is_the_game_tile_wide)
{
    s15::load_texts();
    rv_editor_test_imgui gui(true);
    const measured toolchest = tile_of(gui, { kind::toolchest });
    const measured controls = tile_of(gui, { kind::controls });
    std::fprintf(stderr,
        "  Toolchest %d, Controls %d against the Game tile %d\n",
        toolchest.tile.w,
        controls.tile.w,
        right_column_1280);
    RV_EDITOR_CHECK(toolchest.tile.w <= right_column_1280);
    RV_EDITOR_CHECK(controls.tile.w <= right_column_1280);
}
