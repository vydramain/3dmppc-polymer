// S13.1a, R13, R19: the minimums of the Game, the logs and the control strips let the approved layouts fit, measured at the
// editor's real font and theme.
//  - The Game's least picture width is at most 320 (Scene at 1280 shows 320x240), and its status wraps in at most three lines
//    on the translucent band over the picture, whole, at that width.

#include <algorithm>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "app/rv_editor_shell.hpp"
#include "imgui_internal.h"
#include "layout/rv_editor_tile.hpp"
#include "panes/rv_editor_game_status.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_pane_state.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "font/rv_editor_font.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets_picture.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;
using kind = rv_editor_pane_kind;
namespace fs = std::filesystem;

constexpr int32_t scene_picture_w = 320; // the Game's picture area in Scene at 1280 (R13)

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

float lines(const std::string &text, float width)
{
    return ImGui::CalcTextSize(text.c_str(), nullptr, false, width).y / ImGui::GetTextLineHeight();
}

// Every status the Game and Candidate Playtest can put on the band (the longest numbers), as the pane joins them.
std::vector<std::string> game_statuses()
{
    const uint32_t number = 999999;
    const uint64_t frame = 99999999;
    const char *state_name = "disconnected";
    const float scale = 0.5f;
    const char *mode = "3x";
    const std::string stale = rv_editor_text_format("pane_game.stale_session", std::make_format_args(number, state_name));
    const std::string builds = rv_editor_text_format("pane_game.stopped_builds", std::make_format_args(number));
    const std::string candidate = rv_editor_text_format("pane_game.candidate_image", std::make_format_args(number));
    const std::string states[] = { rv_editor_text("pane_game.stopped_rebuilds"),
        builds,
        rv_editor_text("pane_game.no_candidate"),
        candidate,
        rv_editor_text("pane_game.frame_waiting"),
        rv_editor_text("pane_game.console_disconnected"),
        rv_editor_text("pane_game.not_stopping"),
        rv_editor_text("pane_game.timeout"),
        rv_editor_text("pane_game.playing_keyboard"),
        rv_editor_text("pane_game.click_to_play") };
    const std::string scales[] = { rv_editor_text_format("pane_game.scale_fit_text", std::make_format_args(scale)),
        rv_editor_text_format("pane_game.scale_integer_reduced", std::make_format_args(scale)),
        rv_editor_text_format("pane_game.scale_other_reduced", std::make_format_args(mode, scale)) };
    std::vector<std::string> out;
    for (const std::string &sc : scales) {
        for (const std::string &state : states) {
            const std::string framed = rv_editor_text_format("pane_game.frame_status", std::make_format_args(frame, state));
            for (const std::string &s : { state, framed }) {
                for (const char *tail : { "", stale.c_str() }) {
                    out.push_back(rv_editor_game_status_joined(rv_editor_game_status_compose(320, 240, sc, s, tail)));
                }
            }
        }
    }
    return out;
}

} // namespace

// The Game's pane minimum lets a picture 320 wide (Scene at 1280): its least picture width is at most 320.
RV_EDITOR_TEST(rv_editor_test_s13a_game_least_picture_width_is_at_most_320)
{
    real_gui g;
    g.gui.frame([&] {
        const float w = rv_editor_game_picture_min_width();
        std::fprintf(stderr, "  game least picture width %.1f\n", w);
        RV_EDITOR_CHECK(w > 0.0f);
        RV_EDITOR_CHECK(w <= static_cast<float>(scene_picture_w));
    });
}

// R15, R19: at the least width every status of the Game and of Candidate Playtest wraps in at most three lines, wraps (the
// least width is under what one line needs), and is the least: a pixel less and some status takes four.
RV_EDITOR_TEST(rv_editor_test_s13a_game_status_wraps_in_three_lines_at_the_least_width)
{
    real_gui g;
    g.gui.frame([&] {
        const float w = rv_editor_game_picture_min_width();
        RV_EDITOR_CHECK(w > 0.0f);
        float most = 0.0f, one_line = 0.0f;
        float most_less = 0.0f;
        for (const std::string &text : game_statuses()) {
            most = std::max(most, lines(text, w));
            most_less = std::max(most_less, lines(text, w - 1.0f));
            one_line = std::max(one_line, ImGui::CalcTextSize(text.c_str()).x);
        }
        std::fprintf(stderr,
            "  least width %.1f, longest status %.1f in one line, most lines %.2f (%.2f one pixel less)\n",
            w,
            one_line,
            most,
            most_less);
        RV_EDITOR_CHECK(most <= 3.0f + 0.01f);
        RV_EDITOR_CHECK(most >= 2.0f - 0.01f);     // it does wrap
        RV_EDITOR_CHECK(most_less > 3.0f + 0.01f); // and the width is tight
    });
}

// The band over the picture holds the whole wrapped text, in at most three lines, at the picture width 320: as wide as the
// area, at least as high as the text, at most three lines and their padding.
RV_EDITOR_TEST(rv_editor_test_s13a_game_status_band_holds_the_whole_text_at_the_least_width)
{
    real_gui g;
    g.gui.frame([&] {
        const float w = static_cast<float>(scene_picture_w);
        const float line = ImGui::GetTextLineHeight();
        for (const std::string &text : game_statuses()) {
            RV_EDITOR_CHECK(lines(text, w) <= 3.0f + 0.01f);
            ImGui::SetCursorPos(ImVec2(0, 0));
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const rv_editor_picture_rect area{ at, ImVec2(at.x + w, at.y + 200.0f) };
            const auto band = rv_editor_picture_overlay(text, rv_editor_label_kind::normal, theme, area);
            RV_EDITOR_CHECK(band.max.x - band.min.x >= w - 0.5f);
            RV_EDITOR_CHECK(band.max.y - band.min.y >= lines(text, w) * line - 0.5f);
            RV_EDITOR_CHECK(band.max.y - band.min.y <= 3.0f * line + 2.0f * ImGui::GetStyle().WindowPadding.y + 0.5f);
        }
    });
}

// Every state of the Game and Candidate Playtest (stopped, no project, a running and a paused session on a stand-in console,
// no candidate, a candidate) fits whole in a pane whose picture is 320 wide; the states of one tile keep their rectangles.
RV_EDITOR_TEST(rv_editor_test_s13a_game_states_fit_whole_at_picture_320)
{
    real_gui g;
    stable_tiles::minimum at{ 0, 0 };
    g.gui.frame([&] {
        const rv_editor_size min = rv_editor_game_minimum();
        at = { std::min(static_cast<float>(min.w),
                   static_cast<float>(scene_picture_w) + 2.0f * ImGui::GetStyle().WindowPadding.x),
            static_cast<float>(min.h) };
    });
    rv_editor_shell shell;
    shell.app.project.open = false;
    const std::vector<std::vector<std::function<void()>>> groups = {
        // Without a project: the one row and its button.
        { [&] {
            shell.app.project.open = false;
        } },
        // Stopped, then Burn's Candidate Playtest without and with a candidate (the longest number).
        { [&] {
             shell.app.project.open = true;
             shell.app.release_view = false;
         },
            [&] {
                shell.app.release_view = true;
            },
            [&] {
                rv_editor_candidate c;
                c.number = 999999;
                shell.app.release.candidates.push_back(std::move(c));
                shell.app.release.selected = shell.app.release.candidates.size() - 1;
            } },
    };
    // A live session: the band says the session's state ("waiting for the first frame", "paused", ...), no frame memory needed.
    for (const char *mode : { "running", "paused" }) {
        const fs::path dir =
            fs::temp_directory_path() / (std::string("rv_editor_s13a_") + mode + "_" + std::to_string(getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir);
        rv_editor_shell live;
        live.app.project.open = true;
        live.app.project.root = dir;
        pane_state::fake_console_options options;
        options.mode = mode;
        RV_EDITOR_CHECK(pane_state::start_fake_session(live.app, dir, options));
        const std::string bad = rv_editor_check_pane_states(g.gui, live, kind::game, { [] {} }, at);
        RV_EDITOR_CHECK(bad.empty());
        if (!bad.empty()) {
            std::fprintf(stderr, "  game live (%s) at %.0fx%.0f: %s\n", mode, at.w, at.h, bad.c_str());
        }
        pane_state::end_fake_session(live.app);
        fs::remove_all(dir);
    }
    for (size_t i = 0; i < groups.size(); ++i) {
        const std::string bad = rv_editor_check_pane_states(g.gui, shell, kind::game, groups[i], at);
        RV_EDITOR_CHECK(bad.empty());
        if (!bad.empty()) {
            std::fprintf(stderr, "  game states group %zu at %.0fx%.0f: %s\n", i, at.w, at.h, bad.c_str());
        }
    }
}
