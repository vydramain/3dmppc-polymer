// List rows: ImGui's Selectable in the theme's colours, with the editor's own dim,
// disabled and frozen looks. A list's frame is a well.

#include "imgui_internal.h"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_rows.hpp"
#include "ui/rv_editor_widgets_table.hpp"

namespace rv_editor
{

bool rv_editor_list_begin(const char *id, ImVec2 size, const rv_editor_theme &theme)
{
    return rv_editor_well_begin(id, size, theme);
}

void rv_editor_list_end()
{
    rv_editor_well_end();
}

rv_editor_list_result rv_editor_list_row(const char *id,
    const char *label,
    const rv_editor_theme &theme,
    const rv_editor_list_style &style,
    const rv_editor_state &state)
{
    const bool live = state.look == rv_editor_look::live;
    const bool disabled = state.disabled != nullptr;
    const bool selected = style.selected || rv_editor_row_frozen_fill(state);
    const ImGuiSelectableFlags flags = (style.span_columns ? ImGuiSelectableFlags_SpanAllColumns : ImGuiSelectableFlags_None) |
        (style.overlap ? ImGuiSelectableFlags_AllowOverlap : ImGuiSelectableFlags_None);

    if (disabled) {
        ImGui::BeginDisabled();
    }
    const int colours = rv_editor_row_push_colours(style.selected, style.dim, state, theme);
    ImGui::PushID(id);
    // The whole label: a row wider than its area scrolls it, and the Selectable spans max(label, area).
    // A `whole` row in a table gives the row its own width: ImGui widens a row to its label's width otherwise.
    // A table row that spans its columns is as wide as the area (the inner rect: fixed, whatever the scroll or the table
    // of the last frame was), so its width never feeds back into the auto-fit of column 0; its highlight starts at the
    // table's edge. A by_id row (row select) widens itself.
    const ImGuiWindow *window = ImGui::GetCurrentWindow();
    const bool in_table_span = style.span_columns && !style.by_id && ImGui::GetCurrentTable() != nullptr;
    // A whole row in a table without spanning is as wide as its label: the column fits the widest name instead of keeping
    // the width it had (the area's, which a width taken from the column never grows).
    const bool in_table = ImGui::GetCurrentTable() != nullptr;
    const float row_width = in_table_span ? window->InnerRect.GetWidth() :
        !style.whole                      ? 0.0f :
        style.span_columns                ? window->ParentWorkRect.GetWidth() :
        in_table                          ? 0.0f :
                                            ImGui::GetContentRegionAvail().x;
    const bool pressed = ImGui::Selectable(style.by_id ? label : (std::string(label) + "###row").c_str(),
        selected,
        flags,
        ImVec2(row_width, style.height));
    ImGui::PopID();
    ImGui::PopStyleColor(colours);
    if (disabled) {
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", state.disabled);
    }

    rv_editor_list_result result;
    if (live && !disabled) {
        result.clicked = pressed;
        result.double_clicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        result.right_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    }
    // A live focus is ImGui's nav cursor on the row; a frozen one is drawn like a button's.
    if (state.look == rv_editor_look::focused) {
        rv_editor_draw_focus(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), theme);
    }
    return result;
}

} // namespace rv_editor
