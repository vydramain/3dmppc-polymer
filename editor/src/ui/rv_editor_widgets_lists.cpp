// List rows: ImGui's Selectable in the theme's colours, with the editor's own dim,
// disabled and frozen looks. A list's frame is a well.

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// A frozen hover or press fills in the colour of its own look.
bool rv_editor_list_frozen_fill(const rv_editor_state &state)
{
    return state.look == rv_editor_look::hovered || state.look == rv_editor_look::pressed;
}

// Pushes the colours the row's look needs and returns how many, for one PopStyleColor.
// A frozen look ignores the pointer: hover and press keep the fill the look names.
int rv_editor_list_push_colours(const rv_editor_theme &theme, const rv_editor_list_style &style, const rv_editor_state &state)
{
    const bool pin = state.look != rv_editor_look::live;
    const bool frozen_fill = rv_editor_list_frozen_fill(state);
    const ImGuiCol fill_slot = state.look == rv_editor_look::pressed ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered;
    const ImU32 pinned =
        frozen_fill ? ImGui::GetColorU32(fill_slot) : (style.selected ? ImGui::GetColorU32(ImGuiCol_Header) : 0);
    int count = 0;
    if (pin) {
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, pinned);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, pinned);
        count += 2;
    }
    if (frozen_fill) {
        ImGui::PushStyleColor(ImGuiCol_Header, pinned);
        count += 1;
    }
    if (style.dim) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_disabled));
        count += 1;
    }
    return count;
}

} // namespace

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
    const bool selected = style.selected || rv_editor_list_frozen_fill(state);
    const ImGuiSelectableFlags flags = (style.span_columns ? ImGuiSelectableFlags_SpanAllColumns : ImGuiSelectableFlags_None) |
        (style.overlap ? ImGuiSelectableFlags_AllowOverlap : ImGuiSelectableFlags_None);

    if (disabled) {
        ImGui::BeginDisabled();
    }
    const int colours = rv_editor_list_push_colours(theme, style, state);
    ImGui::PushID(id);
    const bool pressed = ImGui::Selectable(label, selected, flags, ImVec2(0.0f, style.height));
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
