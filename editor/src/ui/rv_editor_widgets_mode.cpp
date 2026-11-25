// The big ask: a card that replaces a tile's content.

#include "ui/rv_editor_widgets_mode.hpp"

#include <algorithm>

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_item_hook.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Text lines the scrolling area keeps at the minimum.
constexpr float min_text_lines = 2.0f;

// The text area's own padding: the text must not touch the area's edge.
float area_pad()
{
    return ImGui::GetStyle().WindowPadding.y;
}

float min_area_height()
{
    // A long text scrolls: the bar needs two arrows and a track as tall as a text line.
    return std::max(min_text_lines * ImGui::GetTextLineHeight() + 2.0f * area_pad(),
        2.0f * ImGui::GetStyle().ScrollbarSize + ImGui::GetTextLineHeight());
}

// Rows under the text area: the gap, the message line, the gap, the button row.
float footer_height()
{
    const float gap = ImGui::GetStyle().ItemSpacing.y;
    return gap + ImGui::GetTextLineHeight() + gap + ImGui::GetFrameHeight();
}

} // namespace

ImVec2 rv_editor_mode_card_min_size(const std::vector<std::string> &buttons, const rv_editor_theme &)
{
    float width = 0.0f;
    for (const std::string &b : buttons) {
        width += rv_editor_button_width(b.c_str());
    }
    if (!buttons.empty()) {
        width += ImGui::GetStyle().ItemSpacing.x * static_cast<float>(buttons.size() - 1);
    }
    const float title = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
    return ImVec2(width, title + min_area_height() + footer_height());
}

int rv_editor_mode_card_begin(const char *id, const rv_editor_mode_card_spec &spec, const rv_editor_theme &theme)
{
    // Never shorter than its own minimum: a tile at its minimum keeps the buttons in its bottom padding, not cut off.
    ImGui::BeginChild(id,
        ImVec2(0, std::max(ImGui::GetContentRegionAvail().y, rv_editor_mode_card_min_size(spec.buttons, theme).y)),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    rv_editor_label(spec.title.c_str(), rv_editor_label_kind::bright, theme, rv_editor_label_layout::line);

    const float area_h = std::max(min_area_height(), ImGui::GetContentRegionAvail().y - footer_height());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rv_editor_col(theme.code_base));
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.code_text));
    // ImGui's own bar, as in the message box: the text area keeps its rectangle whether the bar shows or not.
    ImGui::BeginChild("##text", ImVec2(0, area_h), ImGuiChildFlags_AlwaysUseWindowPadding);
    rv_editor_item_scroll_area();
    if (spec.body) {
        spec.body();
    } else {
        ImGui::TextWrapped("%s", spec.text.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    if (spec.message.empty()) {
        ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight()));
    } else {
        rv_editor_label(spec.message.c_str(), spec.message_kind, theme, rv_editor_label_layout::line);
    }

    int pressed = -1;
    for (size_t i = 0; i < spec.buttons.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        if (rv_editor_button(spec.buttons[i].c_str(), theme)) {
            pressed = static_cast<int>(i);
        }
    }
    return pressed;
}

void rv_editor_mode_card_end()
{
    ImGui::EndChild();
}

} // namespace rv_editor
