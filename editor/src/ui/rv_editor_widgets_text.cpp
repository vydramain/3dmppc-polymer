// Text components: labels in the theme's colours, section headers, separators and tooltips.

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
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

} // namespace

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
    ImGui::SeparatorText(title);
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
