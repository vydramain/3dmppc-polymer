#pragma once

#include <cstdint>

#include "imgui.h"

#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// What a widget is asked to look like. `live` follows the mouse and keyboard;
// the other values freeze one state so the Widget Catalog can show every state
// side by side.
enum class rv_editor_look
{
    live,
    normal,
    hovered,
    pressed,
    focused,
};

struct rv_editor_state
{
    rv_editor_look look = rv_editor_look::live;
    // Why the widget is unavailable. Non-null disables it and becomes its
    // tooltip: a disabled control always says why (UI-04).
    const char *disabled = nullptr;
};

// One interactive rectangle: ImGui's InvisibleButton supplies id, hover,
// press, click and keyboard focus; the widget draws the rest.
struct rv_editor_item
{
    ImVec2 min;
    ImVec2 max;
    bool hovered;
    bool held;
    bool focused;
    bool disabled;
    bool clicked;
};

rv_editor_item rv_editor_item_add(const char *id, ImVec2 size, const rv_editor_state &state);

// Text colour for an item's label.
uint32_t rv_editor_item_text(const rv_editor_theme &theme, const rv_editor_item &item);

// End of the visible part of a label: ImGui's "##" suffix is an id, not text.
const char *rv_editor_label_end(const char *label);

} // namespace rv_editor
