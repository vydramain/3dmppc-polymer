// The message box: a two-line Catppuccin text area over a right-aligned button row, one rectangle in every state.

#include "ui/rv_editor_widgets_message.hpp"

#include <algorithm>
#include <cmath>

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_item_hook.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Text lines the area shows whole.
constexpr float text_lines = 2.0f;
// Characters of text per line the narrowest box keeps.
constexpr float min_chars = 30.0f;
// Padding edges around the text (one per side).
constexpr float padding_sides = 2.0f;

// The text window of a box in a width; the caller owns the id stack.
void rv_editor_message_text_child(const std::string &text, rv_editor_label_kind kind, const rv_editor_theme &theme, float width)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rv_editor_col(theme.code_base));
    ImGui::PushStyleColor(ImGuiCol_Text,
        rv_editor_col(kind == rv_editor_label_kind::normal ? theme.code_text : rv_editor_label_colour(kind, theme)));
    ImGui::BeginChild("##text", ImVec2(width, rv_editor_message_text_height()), ImGuiChildFlags_AlwaysUseWindowPadding);
    rv_editor_item_scroll_area();
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

float rv_editor_message_row_width(const std::vector<std::string> &buttons)
{
    float w = 0.0f;
    for (const auto &b : buttons) {
        w += rv_editor_button_width(b.c_str());
    }
    return buttons.empty() ? 0.0f : w + static_cast<float>(buttons.size() - 1) * ImGui::GetStyle().ItemSpacing.x;
}

} // namespace

float rv_editor_message_text_height()
{
    return text_lines * ImGui::GetTextLineHeight() + padding_sides * ImGui::GetStyle().WindowPadding.y;
}

float rv_editor_message_box_height()
{
    return rv_editor_message_text_height() + ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeight();
}

float rv_editor_message_box_min_width(const std::vector<std::vector<std::string>> &button_rows)
{
    float widest = 0.0f;
    for (const auto &row : button_rows) {
        widest = std::max(widest, rv_editor_message_row_width(row));
    }
    // The text area's padding on both sides and its vertical bar come on top of the 30 characters.
    return widest + min_chars * ImGui::CalcTextSize("0").x + std::floor(ImGui::GetStyle().ScrollbarSize) +
        padding_sides * ImGui::GetStyle().WindowPadding.x;
}

void rv_editor_message_text(const char *id,
    const std::string &text,
    rv_editor_label_kind kind,
    const rv_editor_theme &theme,
    float min_width)
{
    ImGui::PushID(id);
    rv_editor_message_text_child(text, kind, theme, std::max(ImGui::GetContentRegionAvail().x, min_width));
    ImGui::PopID();
}

int rv_editor_message_box(const char *id,
    const std::string &text,
    rv_editor_label_kind kind,
    const std::vector<std::string> &buttons,
    const rv_editor_theme &theme,
    float min_width,
    const std::function<void(float)> &lead,
    const char *button_tooltip)
{
    ImGui::PushID(id);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, min_width);
    rv_editor_message_text_child(text, kind, theme, width);

    // The row's place is kept with no buttons.
    const float row_y = ImGui::GetCursorScreenPos().y;
    const float row_h = ImGui::GetFrameHeight();
    int pressed = -1;
    const float row_width = rv_editor_message_row_width(buttons);
    if (lead) {
        ImGui::SetCursorScreenPos(ImVec2(min.x, row_y));
        lead(width - row_width - (buttons.empty() ? 0.0f : ImGui::GetStyle().ItemSpacing.x));
    }
    float x = min.x + width - row_width;
    for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
        ImGui::SetCursorScreenPos(ImVec2(x, row_y));
        if (rv_editor_button(buttons[i].c_str(), theme)) {
            pressed = i;
        }
        if (button_tooltip != nullptr) {
            rv_editor_item_tooltip(button_tooltip);
        }
        x += rv_editor_button_width(buttons[i].c_str()) + ImGui::GetStyle().ItemSpacing.x;
    }
    ImGui::SetCursorScreenPos(ImVec2(min.x, row_y));
    ImGui::Dummy(ImVec2(width, row_h));
    ImGui::PopID();
    return pressed;
}

} // namespace rv_editor
