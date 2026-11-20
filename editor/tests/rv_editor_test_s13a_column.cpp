// S13.1a, R13, R19: the minimums of the Game, the logs and the control strips let the approved layouts fit, measured at the
// editor's real font and theme.
//  - The Game's least picture width is at most 320 (Scene at 1280 shows 320x240), and its status wraps in at most three lines
//    on the translucent band over the picture, whole, at that width.
//  - At 1280x720 (work area 672 px high) the column Game (picture 300 high, chrome as measured) / control strip / log (with
//    its tabs) fits. Which of the three shrinks is the green's choice.

#include <cstdio>
#include <string>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "layout/rv_editor_tile.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "font/rv_editor_font.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
using kind = rv_editor_pane_kind;

constexpr int32_t work_h = 672;    // the work area of 1280x720
constexpr int32_t picture_h = 300; // the Game's picture area, 400x300

// The context with the editor's fonts, theme and font size, as the shell draws.
struct real_gui {
    rv_editor_test_imgui gui{ true };
    real_gui()
    {
        std::string error;
        RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
        rv_editor_theme_apply(theme, ImGui::GetStyle());
        ImGui::GetStyle().FontSizeBase = rv_editor_font_ui()->LegacySize;
    }
};

// The layout of `preset` after frames in a 1280x720 window: the real minimums, metrics and the settled Game.
struct column {
    rv_editor_shell shell;
    int32_t over_h = 0; // the Game tile's height less its picture area: the chrome
    int32_t bar = 0;
    rv_editor_size game{ 0, 0 }, strip{ 0, 0 }, log{ 0, 0 };

    explicit column(rv_editor_layout_preset preset, rv_editor_test_imgui &gui)
    {
        rv_editor_layout_preset_make(preset, shell.ws.panes, shell.ws.layout);
        shell.ws.focused_leaf = shell.ws.layout.root;
        shell.active = preset;
        shell.app.preset = preset;
        const size_t slot = static_cast<size_t>(preset);
        shell.game_fit[slot] = rv_editor_preset_fits_game(preset);
        shell.layout_untouched[slot] = true;
        shell.app.project.screen_w = 320;
        shell.app.project.screen_h = 240;
        const rv_editor_size win{ 1280, 720 };
        rv_editor_rect work{ 0, 0, 0, 0 };
        for (int frame = 0; frame < 12; ++frame) {
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
                    const float bars = frame_h + frame_h + 2.0f * ImGui::GetStyle().WindowPadding.y;
                    work = { 0, static_cast<int>(frame_h), win.w, static_cast<int>(static_cast<float>(win.h) - bars) };
                    std::string error;
                    rv_editor_workspace_draw(
                        shell.ws,
                        theme,
                        [](void *context, rv_editor_pane_id pane, rv_editor_pane_kind k, const rv_editor_theme &t) {
                            if (k == kind::game) {
                                rv_editor_shell_pane(context, pane, k, t);
                            }
                        },
                        nullptr,
                        &shell,
                        work,
                        error);
                    rv_editor_shell_ui_apply(shell);
                },
                static_cast<float>(win.w),
                static_cast<float>(win.h));
        }
        bar = shell.ws.tile_metrics.bar;
        const auto size_of = [&](kind k) {
            for (size_t i = 0; i < shell.ws.panes.panes.size(); ++i) {
                if (shell.ws.panes.panes[i].kind == k) {
                    return rv_editor_tile_min_size(shell.ws.layout,
                        rv_editor_tile_find(shell.ws.layout, static_cast<rv_editor_pane_id>(i)),
                        shell.ws.tile_metrics,
                        shell.ws.pane_min);
                }
            }
            return rv_editor_size{ 0, 0 };
        };
        game = size_of(kind::game);
        const kind strip_kind = preset == rv_editor_layout_preset::burn ? kind::release_controls : kind::controls;
        const kind log_kind = preset == rv_editor_layout_preset::code ? kind::output :
            preset == rv_editor_layout_preset::debug                  ? kind::runtime_log :
                                                                        kind::build_log;
        strip = size_of(strip_kind);
        log = size_of(log_kind);
        // The chrome from the tile the fit settled: its height less the picture area the Game measured.
        for (size_t i = 0; i < shell.ws.panes.panes.size(); ++i) {
            if (shell.ws.panes.panes[i].kind == kind::game) {
                const rv_editor_rect r =
                    shell.ws.rects[rv_editor_tile_find(shell.ws.layout, static_cast<rv_editor_pane_id>(i))];
                over_h = r.h - shell.ui.game_area.h;
            }
        }
    }
};

const rv_editor_layout_preset column_presets[] = { rv_editor_layout_preset::code,
    rv_editor_layout_preset::debug,
    rv_editor_layout_preset::burn };

const char *name_of(rv_editor_layout_preset p)
{
    return p == rv_editor_layout_preset::code ? "Code" : p == rv_editor_layout_preset::debug ? "Debug" : "Burn";
}

} // namespace

// R13, AC14 at 1280x720: the column of the Game (picture 300 high, with its chrome as the settled Game tile has it), the
// control strip and the log fits the work area, with the bars between.
RV_EDITOR_TEST(rv_editor_test_s13a_the_column_of_game_strip_and_log_fits_672)
{
    real_gui g;
    for (const rv_editor_layout_preset preset : column_presets) {
        column c(preset, g.gui);
        const int32_t sum = picture_h + c.over_h + c.strip.h + c.log.h + 2 * c.bar;
        std::fprintf(stderr,
            "  %s: picture %d + chrome %d + strip %d + log %d + 2 bars of %d = %d, work area %d\n",
            name_of(preset),
            picture_h,
            c.over_h,
            c.strip.h,
            c.log.h,
            c.bar,
            sum,
            work_h);
        RV_EDITOR_CHECK(c.over_h > 0 && c.strip.h > 0 && c.log.h > 0);
        RV_EDITOR_CHECK(sum <= work_h);
    }
}
