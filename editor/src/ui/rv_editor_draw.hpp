#pragma once

#include <cstdint>

#include "imgui.h"

#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// Drawing primitives the ImGui style cannot express. Every shape is built from
// axis-aligned filled rectangles on whole pixels, so nothing is smoothed. Sizes
// in `theme` are unscaled; these functions multiply by theme.scale themselves.

enum class rv_editor_bevel
{
    raised, // light edge top-left, dark edge bottom-right
    sunken, // the same edges swapped: pressed buttons, fields, wells
};

// Two-tone edge inside [min, max).
void rv_editor_draw_bevel(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, rv_editor_bevel kind);

// One-colour edge of bevel width inside [min, max): the brass hover outline.
void rv_editor_draw_frame(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, uint32_t color);

// Filled rectangle with a bevel edge.
void rv_editor_draw_panel(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, uint32_t fill,
    rv_editor_bevel kind);

// Checkerboard of `color` dots on every other scaled pixel: stippled title bars.
void rv_editor_draw_stipple(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, uint32_t color);

// Diamond radio mark centred in [min, max): sunken outline, inset fill, and a
// brass centre when `selected`.
void rv_editor_draw_diamond(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, bool selected);

// Solid pixel triangle pointing `dir`, centred in [min, max).
void rv_editor_draw_arrow(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, ImGuiDir dir,
    uint32_t color);

// 7x7 pixel check mark centred in [min, max).
void rv_editor_draw_check(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, uint32_t color);

// A letter on a coloured tile in [min, max): the stand-in for an icon until
// icons are drawn. The letter is dark on a light colour
// and light on a dark one; a dark outline keeps the tile off the background.
void rv_editor_draw_chip(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, char letter,
    uint32_t color);

// Text in the current font slanted as 4Dwm's window titles: each pixel row drawn
// on its own, the rows above the baseline one pixel further right every two rows.
void rv_editor_draw_text_italic(ImDrawList *dl, ImVec2 pos, ImU32 color, const char *begin, const char *end,
    const rv_editor_theme &theme);

// Dotted one-pixel rectangle just inside [min, max): keyboard focus.
void rv_editor_draw_focus(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme);

} // namespace rv_editor
