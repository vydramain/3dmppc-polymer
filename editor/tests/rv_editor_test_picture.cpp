// The picture area: it takes everything under the shelf whatever the status says, keeps the frame's aspect, and the
// status or note lies on a translucent band inside the picture, one or two lines high.

#include <cmath>
#include <format>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "panes/rv_editor_game_status.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_fits_whole.hpp"
#include "rv_editor_test_imgui.hpp"
#include "rv_editor_test_layout_probe.hpp"
#include "rv_editor_test_stable_tiles.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets_picture.hpp"

using namespace rv_editor;
using namespace rv_editor_test;

namespace
{

constexpr rv_editor_theme theme = rv_editor_theme_olive;

const std::string long_status =
    "320x240  Integer: 0.50x, below 1x to fit  stopped: Run builds the saved files, then starts that build here.";
const std::string note = "Scene is read-only: it is open in another editor.";

float lines(const std::string &text, float width)
{
    return ImGui::CalcTextSize(text.c_str(), nullptr, false, width).y / ImGui::GetTextLineHeight();
}

// A shelf stand-in, then the picture with `text` over it; returns the area.
rv_editor_picture_rect draw_picture(const std::string &text, bool clickable = false)
{
    ImGui::Button("shelf");
    const rv_editor_picture_rect area = rv_editor_picture_begin("##pic", theme, clickable);
    rv_editor_picture_overlay(text, rv_editor_label_kind::normal, theme, area);
    rv_editor_picture_end();
    return area;
}

} // namespace

// AC16, R15: the picture takes all the room under the shelf, to the window's content edge on both axes.
RV_EDITOR_TEST(rv_editor_test_picture_takes_all_the_room_under_the_shelf)
{
    rv_editor_test_imgui gui;
    for (const ImVec2 size : { ImVec2(300, 160), ImVec2(760, 420), ImVec2(480, 300) }) {
        for (const bool clickable : { false, true }) {
            gui.frame(
                [&] {
                    ImGui::Button("shelf");
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    const ImVec2 avail = ImGui::GetContentRegionAvail();
                    const auto area = rv_editor_picture_begin("##pic", theme, clickable);
                    RV_EDITOR_CHECK(area.min.x == at.x && area.min.y == at.y);
                    RV_EDITOR_CHECK(area.max.x == at.x + avail.x && area.max.y == at.y + avail.y);
                    // The one item it submits is the whole area.
                    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
                    RV_EDITOR_CHECK(lo.x == area.min.x && lo.y == area.min.y && hi.x == area.max.x && hi.y == area.max.y);
                    rv_editor_picture_end();
                },
                size.x,
                size.y);
        }
    }
}

// AC16: the same picture rectangle and the same item rectangles with an empty status, a long status and a note.
RV_EDITOR_TEST(rv_editor_test_picture_height_does_not_depend_on_the_text)
{
    rv_editor_test_imgui gui;
    rv_editor_probe_record first;
    rv_editor_picture_rect first_area{};
    const std::vector<std::string> texts{ "", long_status, note };
    for (size_t i = 0; i < texts.size(); ++i) {
        rv_editor_picture_rect area{};
        rv_editor_probe_record record = rv_editor_probe_draw(
            gui,
            300,
            [&](rv_editor_probe_record &) {
                area = draw_picture(texts[i]);
            },
            200);
        if (i == 0) {
            first = record;
            first_area = area;
            RV_EDITOR_CHECK(area.max.y - area.min.y > 100.0f);
            continue;
        }
        RV_EDITOR_CHECK(area.min.y == first_area.min.y && area.max.y == first_area.max.y);
        RV_EDITOR_CHECK(area.min.x == first_area.min.x && area.max.x == first_area.max.x);
        RV_EDITOR_CHECK(stable_tiles::same_rects(first, record).empty());
    }
}

// AC16: the status and the note are drawn inside the picture's rectangle, on a translucent backing from its top.
RV_EDITOR_TEST(rv_editor_test_picture_overlay_lies_inside_the_picture)
{
    rv_editor_test_imgui gui;
    const std::vector<std::pair<std::string, rv_editor_label_kind>> cases{ { "Stopped.", rv_editor_label_kind::normal },
        { note, rv_editor_label_kind::warning },
        { "320x240  Fit 2.00x  frame 4312, click the picture to play", rv_editor_label_kind::dim } };
    for (const auto &[text, kind] : cases) {
        gui.frame(
            [&] {
                ImGui::Button("shelf");
                const auto area = rv_editor_picture_begin("##pic", theme, false);
                ImDrawList *dl = ImGui::GetWindowDrawList();
                dl->AddImage(ImTextureID(1), area.min, area.max);
                const int image_end = dl->VtxBuffer.Size;
                const ImGuiID item = ImGui::GetItemID();
                const ImVec2 cursor = ImGui::GetCursorScreenPos();
                const auto band = rv_editor_picture_overlay(text, kind, theme, area);
                RV_EDITOR_CHECK(band.min.x == area.min.x && band.min.y == area.min.y);
                RV_EDITOR_CHECK(band.max.x == area.max.x && band.max.y <= area.max.y);
                const float h = band.max.y - band.min.y;
                RV_EDITOR_CHECK(h == ImGui::CalcTextSize(text.c_str(), nullptr, false, area.max.x - area.min.x).y);
                RV_EDITOR_CHECK(h > 0.0f && h <= 2.0f * ImGui::GetTextLineHeight());
                // No item, no cursor move: it only draws.
                RV_EDITOR_CHECK(ImGui::GetItemID() == item);
                RV_EDITOR_CHECK(ImGui::GetCursorScreenPos().x == cursor.x && ImGui::GetCursorScreenPos().y == cursor.y);
                // Drawn after the picture: the translucent backing, then glyphs in the kind's colour, all inside the area.
                RV_EDITOR_CHECK(dl->VtxBuffer.Size > image_end + 4);
                for (int i = image_end; i < dl->VtxBuffer.Size; ++i) {
                    const ImDrawVert &v = dl->VtxBuffer[i];
                    RV_EDITOR_CHECK(v.pos.x >= area.min.x && v.pos.x <= area.max.x);
                    RV_EDITOR_CHECK(v.pos.y >= area.min.y && v.pos.y <= area.max.y);
                    if (i < image_end + 4) {
                        const ImU32 alpha = v.col >> IM_COL32_A_SHIFT;
                        RV_EDITOR_CHECK(alpha > 0 && alpha < 255);
                    } else {
                        RV_EDITOR_CHECK(v.col == rv_editor_col(rv_editor_label_colour(kind, theme)));
                    }
                }
                rv_editor_picture_end();
            },
            400,
            300);
    }
}

// An empty status draws no band at all.
RV_EDITOR_TEST(rv_editor_test_picture_overlay_of_an_empty_text_draws_nothing)
{
    rv_editor_test_imgui gui;
    gui.frame([&] {
        ImGui::Button("shelf");
        const auto area = rv_editor_picture_begin("##pic", theme, false);
        const int before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        const auto band = rv_editor_picture_overlay("", rv_editor_label_kind::normal, theme, area);
        RV_EDITOR_CHECK(band.max.y == band.min.y);
        RV_EDITOR_CHECK(ImGui::GetWindowDrawList()->VtxBuffer.Size == before);
        rv_editor_picture_end();
    });
}

// AC16: 320x240 keeps its proportions to 1 px, fills the limiting side, stays in the area and is centred.
RV_EDITOR_TEST(rv_editor_test_picture_frame_keeps_the_aspect)
{
    for (const ImVec2 size : { ImVec2(300, 200), ImVec2(640, 480), ImVec2(900, 300), ImVec2(300, 600), ImVec2(747, 421) }) {
        const rv_editor_picture_rect area{ ImVec2(10, 20), ImVec2(10 + size.x, 20 + size.y) };
        const auto f = rv_editor_picture_frame(area, 320, 240);
        const float w = f.max.x - f.min.x, h = f.max.y - f.min.y;
        RV_EDITOR_CHECK(w > 0.0f && h > 0.0f);
        RV_EDITOR_CHECK(std::fabs(h - w * 240.0f / 320.0f) <= 1.0f);
        RV_EDITOR_CHECK(f.min.x >= area.min.x && f.min.y >= area.min.y && f.max.x <= area.max.x && f.max.y <= area.max.y);
        RV_EDITOR_CHECK(std::fabs(w - size.x) <= 1.0f || std::fabs(h - size.y) <= 1.0f);
        RV_EDITOR_CHECK(std::fabs((f.min.x - area.min.x) - (area.max.x - f.max.x)) <= 1.0f);
        RV_EDITOR_CHECK(std::fabs((f.min.y - area.min.y) - (area.max.y - f.max.y)) <= 1.0f);
    }
}

// R15: the least width wraps the longest text into two lines and is tight to 1 px.
RV_EDITOR_TEST(rv_editor_test_picture_min_width_fits_the_longest_text_in_two_lines)
{
    rv_editor_test_imgui gui;
    gui.frame([&] {
        RV_EDITOR_CHECK(rv_editor_picture_min_width({}) == 0.0f);
        const std::vector<std::string> texts{ "Stopped.", note, long_status };
        const float w = rv_editor_picture_min_width(texts);
        RV_EDITOR_CHECK(w > 0.0f);
        for (const auto &t : texts) {
            RV_EDITOR_CHECK(lines(t, w) <= 2.0f + 0.01f);
        }
        RV_EDITOR_CHECK(lines(long_status, w - 1.0f) > 2.0f + 0.01f);
        // More short texts change nothing; a longer one only widens it.
        RV_EDITOR_CHECK(rv_editor_picture_min_width({ "a", long_status }) == rv_editor_picture_min_width({ long_status }));
        RV_EDITOR_CHECK(rv_editor_picture_min_width({ long_status + " and then some more words of text" }) >= w);
    });
}

// R15: the Game's minimum width is made from its longest status template, in every state the pane can show.
RV_EDITOR_TEST(rv_editor_test_game_picture_min_width_covers_every_status)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    gui.frame([&] {
        const float w = rv_editor_game_picture_min_width();
        RV_EDITOR_CHECK(w > 0.0f);
        const char *states[] = { "pane_game.stopped_rebuilds",
            "pane_game.stopped_builds",
            "pane_game.no_candidate",
            "pane_game.candidate_image",
            "pane_game.frame_waiting",
            "pane_game.console_disconnected",
            "pane_game.not_stopping",
            "pane_game.timeout",
            "pane_game.playing_keyboard",
            "pane_game.click_to_play" };
        const uint32_t number = 123;
        const char *state_name = "disconnected";
        const std::string stale = rv_editor_text_format("pane_game.stale_session", std::make_format_args(number, state_name));
        for (const char *scale : { "Fit 2.37x", "Integer: 0.50x, below 1x to fit", "3x, lowered to 0.50x to fit" }) {
            for (const char *key : states) {
                const std::string state = rv_editor_text(key);
                for (const char *tail : { "", stale.c_str() }) {
                    const auto l = rv_editor_game_status_compose(320, 240, scale, state, tail);
                    RV_EDITOR_CHECK(lines(rv_editor_game_status_joined(l), w) <= 2.0f + 0.01f);
                }
            }
        }
    });
}

// AC16, R15: the Game tile at its minimum fits whole, and that minimum is as wide as the longest status needs.
RV_EDITOR_TEST(rv_editor_test_game_fits_whole_at_its_minimum)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    rv_editor_test_imgui gui;
    const auto min = stable_tiles::pane_minimum(gui, rv_editor_pane_kind::game);
    RV_EDITOR_CHECK(rv_editor_game_picture_min_width() > 0.0f);
    RV_EDITOR_CHECK(min.w > 0.0f && min.w >= rv_editor_game_picture_min_width());
    if (min.w <= 0.0f) {
        return; // no minimum width to draw in yet
    }
    rv_editor_shell shell;
    shell.app.project.open = true;
    const std::string bad = rv_editor_check_pane_states(gui, shell, rv_editor_pane_kind::game, { [] {} }, min);
    RV_EDITOR_CHECK(bad.empty());
    if (!bad.empty()) {
        std::fprintf(stderr, "  game at its minimum: %s\n", bad.c_str());
    }
}
