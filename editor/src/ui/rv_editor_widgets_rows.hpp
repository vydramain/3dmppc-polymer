#pragma once

#include "imgui.h"

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widget_item.hpp"

namespace rv_editor
{

// Shared by tree and list rows (internal to ui/). A frozen hover or press fills in the colour of its own look.
inline bool rv_editor_row_frozen_fill(const rv_editor_state &state)
{
    return state.look == rv_editor_look::hovered || state.look == rv_editor_look::pressed;
}

// Pushes the colours the row's look needs and returns how many, for one PopStyleColor.
// A frozen look ignores the pointer: hover and press keep the fill the look names.
inline int rv_editor_row_push_colours(bool selected, bool dim, const rv_editor_state &state, const rv_editor_theme &theme)
{
    const bool pin = state.look != rv_editor_look::live;
    const bool frozen_fill = rv_editor_row_frozen_fill(state);
    const ImGuiCol fill_slot = state.look == rv_editor_look::pressed ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered;
    const ImU32 pinned = frozen_fill ? ImGui::GetColorU32(fill_slot) : (selected ? ImGui::GetColorU32(ImGuiCol_Header) : 0);
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
    if (dim) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.text_disabled));
        count += 1;
    }
    return count;
}

} // namespace rv_editor
