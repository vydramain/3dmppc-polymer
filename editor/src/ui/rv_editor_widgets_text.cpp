// Text components: labels in the theme's colours, section headers, separators and tooltips.

#include <cmath>

#include "imgui_internal.h"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

// The colour a label kind draws in. No default: a new kind is a warning here.
uint32_t rv_editor_label_colour(rv_editor_label_kind kind, const rv_editor_theme &theme)
{
    switch (kind) {
    case rv_editor_label_kind::normal:
        return theme.text;
    case rv_editor_label_kind::dim:
        return theme.text_disabled;
    case rv_editor_label_kind::bright:
        return theme.text_bright;
    case rv_editor_label_kind::info:
        return theme.code_blue;
    case rv_editor_label_kind::warning:
        return theme.warning;
    case rv_editor_label_kind::error:
        return theme.error;
    case rv_editor_label_kind::on_selection:
        return theme.dark;
    }
    return theme.text;
}

void rv_editor_label(const char *text, rv_editor_label_kind kind, const rv_editor_theme &theme, rv_editor_label_layout layout)
{
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(rv_editor_label_colour(kind, theme)));
    switch (layout) {
    case rv_editor_label_layout::line:
        ImGui::TextUnformatted(text);
        break;
    case rv_editor_label_layout::wrap:
        ImGui::TextWrapped("%s", text);
        break;
    case rv_editor_label_layout::bullet:
        // Bullet keeps the cursor on its line, so the wrapped text starts beside it.
        ImGui::Bullet();
        ImGui::TextWrapped("%s", text);
        break;
    }
    ImGui::PopStyleColor();
}

void rv_editor_section(const char *title)
{
    // Not ImGui::SeparatorText: it cuts a title wider than its area to "...". The title is laid out whole at its text
    // width, so a page of sections scrolls sideways.
    ImDrawList *list = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::CalcTextSize(title);
    const float pad = ImGui::GetStyle().SeparatorTextPadding.y; // the room ImGui's own section title takes
    ImGui::ItemSize(ImVec2(size.x, size.y + pad * 2.0f));
    if (ImGui::ItemAdd(ImRect(at, ImVec2(at.x + size.x, at.y + size.y + pad * 2.0f)), 0)) {
        list->AddText(ImVec2(at.x, at.y + pad), ImGui::GetColorU32(ImGuiCol_Text), title);
    }
    const float from = at.x + size.x + ImGui::GetStyle().ItemSpacing.x;
    const float to = at.x + ImGui::GetContentRegionAvail().x;
    if (to > from) {
        const float y = std::floor(at.y + pad + size.y / 2.0f);
        list->AddLine(ImVec2(from, y), ImVec2(to, y), ImGui::GetColorU32(ImGuiCol_Separator));
    }
}

bool rv_editor_collapsing_section(const char *title)
{
    return ImGui::CollapsingHeader(title);
}

void rv_editor_separator()
{
    ImGui::Separator();
}

void rv_editor_tooltip(const char *text)
{
    ImGui::SetTooltip("%s", text);
}

void rv_editor_item_tooltip(const char *text)
{
    ImGui::SetItemTooltip("%s", text);
}

} // namespace rv_editor
