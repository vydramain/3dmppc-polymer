// Game status layout: one line when it fits, size and state on separate lines otherwise, and the picture
// starts under the last drawn line.

#include "panes/rv_editor_game_status.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"

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

RV_EDITOR_TEST(rv_editor_test_game_status_draw_returns_bottom_of_wrapped_rows)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    const std::vector<std::string> rows{ "320x240  Fit 2.00x", "Stopped. stale: session 3, Exited with a long state" };
    gui.frame([&] {
        ImGui::BeginChild("narrow", ImVec2(70, 250));
        const float top = ImGui::GetCursorScreenPos().y;
        const float bottom = rv_editor_game_status_draw(rows, rv_editor_label_kind::normal, theme);
        const float last_item_bottom = ImGui::GetItemRectMax().y;
        // Narrow enough that the state wrapped over at least three lines in all.
        RV_EDITOR_CHECK(last_item_bottom - top > 3.0f * ImGui::GetTextLineHeight());
        RV_EDITOR_CHECK(bottom >= last_item_bottom);
        ImGui::EndChild();
    });
}
