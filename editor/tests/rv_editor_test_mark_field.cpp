// The mark note field: at the narrowest row its hint fits inside the field and the field does not run under the
// first tile.

#include <string>

#include "app/rv_editor_app.hpp"
#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_pane_mark_tools.hpp"
#include "rv_editor_test.hpp"
#include "rv_editor_test_imgui.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_tile.hpp"

using namespace rv_editor;

namespace
{

// The width of the three small tiles of the row without Restart.
float small_tiles_width(const rv_editor_theme &theme)
{
    const char *const ids[] = { "pane_toolchest.mark_button", "pane_toolchest.capture_button", "pane_toolchest.report_button" };
    const char *const glyphs[] = { rv_editor_glyph::mark_moment,
        rv_editor_glyph::capture_frame,
        rv_editor_glyph::report_issue };
    float sum = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const rv_editor_tile tile = { rv_editor_text(ids[i]), glyphs[i], theme.code_yellow, "", nullptr };
        sum += rv_editor_tile_width(tile, rv_editor_tile_size::small);
    }
    return sum;
}

} // namespace

RV_EDITOR_TEST(rv_editor_test_mark_placeholder_text)
{
    RV_EDITOR_CHECK(std::string(rv_editor_text("pane_toolchest.mark_placeholder")) == "Mark note...");
}

RV_EDITOR_TEST(rv_editor_test_mark_tools_min_width_fits_the_hint)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    float min_width = 0.0f;
    float need = 0.0f;
    gui.frame([&] {
        min_width = rv_editor_mark_tools_min_width(theme, false);
        need = ImGui::CalcTextSize(rv_editor_text("pane_toolchest.mark_placeholder")).x +
            2.0f * ImGui::GetStyle().FramePadding.x + small_tiles_width(theme);
    });
    RV_EDITOR_CHECK(min_width >= need);
}

RV_EDITOR_TEST(rv_editor_test_mark_field_at_min_width_holds_the_hint_and_clears_the_tiles)
{
    rv_editor_test::rv_editor_test_imgui gui;
    const rv_editor_theme theme = rv_editor_theme_olive;
    rv_editor_app app;
    rv_editor_ui state;
    std::vector<rv_editor_ui_request> requests;
    const rv_editor_ui_port ui{ state, requests };
    std::vector<rv_editor_tile_rect> tiles;
    float field_left = 0.0f;
    float hint_w = 0.0f;
    gui.frame([&] {
        const float w = rv_editor_mark_tools_min_width(theme, false);
        // A child exactly as wide as the narrowest row, without padding.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("row", ImVec2(w, 40), ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar();
        field_left = ImGui::GetCursorScreenPos().x;
        tiles = rv_editor_mark_tools(app, theme, ui, false);
        ImGui::EndChild();
        hint_w =
            ImGui::CalcTextSize(rv_editor_text("pane_toolchest.mark_placeholder")).x + 2.0f * ImGui::GetStyle().FramePadding.x;
    });
    RV_EDITOR_CHECK(tiles.size() == 3);
    if (tiles.empty()) {
        return;
    }
    // The field ends one item gap before the first tile.
    const float field_right = tiles.front().left - ImGui::GetStyle().ItemSpacing.x;
    RV_EDITOR_CHECK(field_right - field_left >= hint_w);
    RV_EDITOR_CHECK(tiles.front().left >= field_right);
}
