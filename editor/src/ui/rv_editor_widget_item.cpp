#include "ui/rv_editor_widget_item.hpp"

#include <cstring>

namespace rv_editor
{

rv_editor_item rv_editor_item_add(const char *id, ImVec2 size, const rv_editor_state &state)
{
    rv_editor_item item = {};
    item.disabled = state.disabled != nullptr;

    if (item.disabled) {
        ImGui::BeginDisabled();
    }
    item.clicked = ImGui::InvisibleButton(id, size);
    item.min = ImGui::GetItemRectMin();
    item.max = ImGui::GetItemRectMax();
    item.hovered = ImGui::IsItemHovered();
    item.held = ImGui::IsItemActive();
    item.focused = ImGui::IsItemFocused();
    if (item.disabled) {
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", state.disabled);
    }

    switch (state.look) {
    case rv_editor_look::live:
        return item;
    case rv_editor_look::normal:
        item.hovered = false;
        item.held = false;
        item.focused = false;
        break;
    case rv_editor_look::hovered:
        item.hovered = true;
        item.held = false;
        item.focused = false;
        break;
    case rv_editor_look::pressed:
        item.hovered = true;
        item.held = true;
        item.focused = false;
        break;
    case rv_editor_look::focused:
        item.hovered = false;
        item.held = false;
        item.focused = true;
        break;
    }
    // A frozen look is a picture of a state, not a control.
    item.clicked = false;
    return item;
}

uint32_t rv_editor_item_text(const rv_editor_theme &theme, const rv_editor_item &item)
{
    return item.disabled ? theme.text_disabled : theme.text;
}

const char *rv_editor_label_end(const char *label)
{
    const char *hash = std::strstr(label, "##");
    return hash != nullptr ? hash : label + std::strlen(label);
}

} // namespace rv_editor
