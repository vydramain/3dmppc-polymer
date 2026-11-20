// The mark tools row: Runtime Controls draws Mark, Capture and Report, the Session Toolchest adds Restart; every
// tile has the same top and bottom, and the row stays inside the shelf. The hint in Session names Runtime Controls.

#include <string>

#include "app/rv_editor_app.hpp"
#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_pane_mark_tools.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

using namespace rv_editor;

namespace
{

// Draws the row inside a shelf; reports the shelf's content right edge and the tiles.
std::vector<rv_editor_tile_rect> draw_row(bool with_restart, float &content_right)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    rv_editor_app app;
    rv_editor_ui state;
    std::vector<rv_editor_ui_request> requests;
    const rv_editor_ui_port ui{ state, requests };
    std::vector<rv_editor_tile_rect> tiles;
    gui.frame([&] {
        rv_editor_shelf_begin("shelf", theme);
        content_right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        tiles = rv_editor_mark_tools(app, theme, ui, with_restart);
        rv_editor_shelf_end();
    });
    return tiles;
}

void check_row(bool with_restart, size_t count)
{
    float right = 0.0f;
    const auto tiles = draw_row(with_restart, right);
    RV_EDITOR_CHECK(tiles.size() == count);
    for (const auto &t : tiles) {
        RV_EDITOR_CHECK(t.top == tiles.front().top);
        RV_EDITOR_CHECK(t.bottom == tiles.front().bottom);
        RV_EDITOR_CHECK(t.bottom > t.top);
    }
    RV_EDITOR_CHECK(!tiles.empty() && tiles.back().right <= right);
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_marks_help_names_runtime_controls)
{
    const std::string help = rv_editor_text("pane_session.marks_help");
    RV_EDITOR_CHECK(help.find("Runtime Controls") != std::string::npos);
    RV_EDITOR_CHECK(help.find("Toolchest") == std::string::npos);
}

RV_EDITOR_TEST(rv_editor_test_mark_tools_without_restart_has_three_tiles)
{
    check_row(false, 3);
}

RV_EDITOR_TEST(rv_editor_test_mark_tools_with_restart_has_four_tiles)
{
    check_row(true, 4);
}
