// Game status layout: one line when it fits, size and state on separate lines otherwise; the rows lie over the top
// of the picture on a translucent backing as tall as they are, and the picture takes the whole area.

#include "panes/rv_editor_game_status.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"

#include <format>
#include <string>
#include <vector>

using namespace rv_editor;

RV_EDITOR_TEST(rv_editor_test_game_status_rows_one_line_when_it_fits)
{
    const rv_editor_game_status_lines lines{ "320x240  Fit 2.00x", "Stopped." };
    const std::string joined = "320x240  Fit 2.00x  Stopped.";
    for (const float width : { 200.0f, 201.0f, 1000.0f }) {
        const auto rows = rv_editor_game_status_rows(lines, width, 200.0f);
        RV_EDITOR_CHECK(rows.size() == 1);
        RV_EDITOR_CHECK(!rows.empty() && rows[0] == joined);
    }
}

RV_EDITOR_TEST(rv_editor_test_game_status_rows_split_when_too_narrow)
{
    const rv_editor_game_status_lines lines{ "320x240  Fit 2.00x", "Stopped." };
    const auto rows = rv_editor_game_status_rows(lines, 199.0f, 200.0f);
    RV_EDITOR_CHECK(rows.size() == 2);
    RV_EDITOR_CHECK(rows.size() == 2 && rows[0] == "320x240  Fit 2.00x");
    RV_EDITOR_CHECK(rows.size() == 2 && rows[1] == "Stopped.");
}

RV_EDITOR_TEST(rv_editor_test_game_status_stale_session_text)
{
    std::string error;
    RV_EDITOR_CHECK(rv_editor_text_load({}, error) == 0);
    const int session = 3;
    const char *state = "Exited";
    RV_EDITOR_CHECK(
        rv_editor_text_format("pane_game.stale_session", std::make_format_args(session, state)) == "stale: session 3, Exited");
}

namespace
{

bool same_view(const rv_editor_game_view &a, const rv_editor_game_view &b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.scale == b.scale && a.reduced == b.reduced;
}

} // namespace

// AC13: the picture is placed from the full area height, so the text does not change its scale.
RV_EDITOR_TEST(rv_editor_test_game_picture_view_uses_full_height)
{
    rv_editor_test::rv_editor_test_imgui gui;
    gui.frame([&] {
        const auto view = rv_editor_game_picture_view(320, 240, 640.0f, 480.0f, rv_editor_game_scale::fit);
        RV_EDITOR_CHECK(view.scale == 2.0f);
        RV_EDITOR_CHECK(view.w == 640.0f && view.h == 480.0f);
        for (const auto mode : { rv_editor_game_scale::fit, rv_editor_game_scale::integer, rv_editor_game_scale::x1 }) {
            for (const ImVec2 area : { ImVec2(640, 480), ImVec2(300, 200), ImVec2(900, 400), ImVec2(1000, 900) }) {
                RV_EDITOR_CHECK(same_view(rv_editor_game_picture_view(320, 240, area.x, area.y, mode),
                    rv_editor_game_place(320, 240, area.x, area.y, mode)));
            }
        }
    });
}

// AC16: a click anywhere over the picture, where the text lies too, hits the click area.
RV_EDITOR_TEST(rv_editor_test_game_click_rect_covers_text)
{
    rv_editor_test::rv_editor_test_imgui gui;
    gui.frame([&] {
        const ImVec2 origin(10.0f, 20.0f), area(300.0f, 200.0f);
        const auto click = rv_editor_game_click_rect(origin, area);
        RV_EDITOR_CHECK(click.min.x == origin.x && click.min.y == origin.y);
        RV_EDITOR_CHECK(click.max.x == origin.x + area.x && click.max.y == origin.y + area.y);
    });
}
