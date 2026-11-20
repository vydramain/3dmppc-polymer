// Runtime Controls' strip measures: its least width and its height at a width.

#include <algorithm>

#include "imgui.h"
#include "panes/rv_editor_pane_controls.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// The longest message a state shows: `Build #999999 step 99/99: ` and a 16 character step name.
constexpr const char *longest_message = "Build #999999 step 99/99: xxxxxxxxxxxxxxxx";

// The UI font is current while measuring: main sets the minimums outside a frame, where ImGui has no current font.
struct ui_font_scope {
    ui_font_scope()
    {
        ImFont *font = ImGui::GetIO().Fonts->Fonts[0];
        ImGui::PushFont(font, font->LegacySize);
    }
    ~ui_font_scope()
    {
        ImGui::PopFont();
    }
    ui_font_scope(const ui_font_scope &) = delete;
    ui_font_scope &operator=(const ui_font_scope &) = delete;
};

} // namespace

float rv_editor_transport_column_min_width()
{
    const ui_font_scope font;
    const float slot = rv_editor_controls_action_slot_width(rv_editor_theme_olive);
    // The column's two rows are as wide as the column: the slot or the longest message, whichever is wider.
    return std::max(slot, ImGui::CalcTextSize(longest_message).x);
}

float rv_editor_transport_min_width()
{
    const ui_font_scope font;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float gap = style.ItemSpacing.x;
    const float buttons = 6.0f * ImGui::GetFrameHeight() + 5.0f * gap;
    // The window's left padding and the shelf's, the buttons, then the right column; the column's rows reach the tile's
    // edge at this width (they never narrow below it), so no right padding is counted.
    return style.WindowPadding.x + style.ItemInnerSpacing.x + buttons + gap + rv_editor_transport_column_min_width();
}

float rv_editor_transport_height(float width)
{
    const ui_font_scope font;
    const ImGuiStyle &style = ImGui::GetStyle();
    // The window's padding, the shelf's padding on both sides (the style's inner spacing is the theme's pad), the taller
    // of the tiles and the right column's two rows (slot with the note, the message line), and the spacing the tile's content
    // ends with.
    const float right = ImGui::GetFrameHeight() + style.ItemSpacing.y + ImGui::GetTextLineHeight();
    return style.WindowPadding.y + 2.0f * style.ItemInnerSpacing.y +
        std::max(rv_editor_transport_side(width, rv_editor_transport_column_min_width()), right) + style.ItemSpacing.y;
}

} // namespace rv_editor
