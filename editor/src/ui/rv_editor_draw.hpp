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

// Dotted one-pixel rectangle just inside [min, max): keyboard focus.
void rv_editor_draw_focus(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme);

} // namespace rv_editor
