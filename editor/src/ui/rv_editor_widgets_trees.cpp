// Tree rows: ImGui's TreeNodeEx in the theme's colours, with the editor's own dim,
// disabled and frozen looks.

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_rows.hpp"

namespace rv_editor
{

rv_editor_tree_result rv_editor_tree_row(const char *id,
    const char *label,
    const rv_editor_theme &theme,
    const rv_editor_tree_style &style,
    const rv_editor_state &state)
{
    const bool live = state.look == rv_editor_look::live;
    const bool disabled = state.disabled != nullptr;
    const bool frozen_fill = rv_editor_row_frozen_fill(state);

    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
        (style.leaf ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_None) |
        (style.no_push ? ImGuiTreeNodeFlags_NoTreePushOnOpen : ImGuiTreeNodeFlags_None) |
        ((style.selected || frozen_fill) ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None) |
        (style.default_open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None) |
        (style.open_on_double_click ? ImGuiTreeNodeFlags_OpenOnDoubleClick : ImGuiTreeNodeFlags_None) |
        (style.lines ? ImGuiTreeNodeFlags_DrawLinesFull : ImGuiTreeNodeFlags_None);
    if (style.open.has_value()) {
        ImGui::SetNextItemOpen(*style.open);
    }

    if (disabled) {
        ImGui::BeginDisabled();
    }
    const int colours = rv_editor_row_push_colours(style.selected, style.dim, state, theme);
    const bool expanded = ImGui::TreeNodeEx(id, flags, "%s", label);
    ImGui::PopStyleColor(colours);
    if (disabled) {
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", state.disabled);
    }

    rv_editor_tree_result result;
    result.open = expanded && !style.no_push;
    if (live) {
        result.toggled = ImGui::IsItemToggledOpen();
        result.clicked = !disabled && ImGui::IsItemClicked() && !result.toggled;
        result.right_clicked = !disabled && ImGui::IsItemClicked(ImGuiMouseButton_Right);
    }
    // A live focus is ImGui's nav cursor on the row; a frozen one is drawn like a button's.
    if (state.look == rv_editor_look::focused) {
        rv_editor_draw_focus(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), theme);
    }
    return result;
}

void rv_editor_tree_pop()
{
    ImGui::TreePop();
}

} // namespace rv_editor
