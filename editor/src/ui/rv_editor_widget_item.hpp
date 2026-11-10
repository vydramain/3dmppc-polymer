#pragma once

#include <cstdint>

#include "imgui.h"

#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// What a widget is asked to look like. `live` follows the mouse and keyboard;
// the other values freeze one state so the Widget Catalog can show every state
// side by side.
enum class rv_editor_look {
    live,
    normal,
    hovered,
    pressed,
    focused,
};

struct rv_editor_state {
    rv_editor_look look = rv_editor_look::live;
    // Why the widget is unavailable. Non-null disables it and becomes its
    // tooltip: a disabled control always says why.
    const char *disabled = nullptr;
};

// One interactive rectangle: ImGui's InvisibleButton supplies id, hover,
// press, click and keyboard focus; the widget draws the rest.
struct rv_editor_item {
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

// End of the longest prefix of [text, end) that still fits `width` with "..."
// after it, or `end` when the whole text fits. Never splits a UTF-8 character.
const char *rv_editor_text_fit(const char *text, const char *end, float width);

// Blend two 0xRRGGBB colours by averaging each channel.
uint32_t rv_editor_blend_color(uint32_t color1, uint32_t color2);

// The shared face of every push button: a dark outline, the bevel, a hover frame
// and focus dots. A selected one is filled with the selection colour even when
// pressed, and its hover frame is the light text colour, not the selection colour.
void rv_editor_button_face(ImDrawList *dl,
    const rv_editor_item &item,
    const rv_editor_theme &t,
    bool down,
    bool selected = false);

// A letter button's item: a square of frame height with its tooltip, unless disabled.
rv_editor_item rv_editor_letter_add(const char *id, const char *tooltip, const rv_editor_state &state);

// A letter button's face and code; `down` sinks and nudges it, `selected` fills the face in the selection colour.
void rv_editor_letter_draw(const rv_editor_item &item,
    const char *code,
    uint32_t color,
    const rv_editor_theme &theme,
    bool down,
    bool selected);

} // namespace rv_editor
