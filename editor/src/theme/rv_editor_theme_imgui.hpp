#pragma once

#include <cstdint>

#include "imgui.h"

#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// 0xRRGGBB token -> opaque ImGui colour.
constexpr ImU32 rv_editor_col(uint32_t rgb)
{
    return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 0xff);
}

// Writes the tokens into an ImGui style: colours, square corners, no
// antialiasing, sizes multiplied by the theme's integer scale. Stock ImGui
// widgets then draw in the palette; what a style cannot express (two-tone
// bevels, diamonds, stipple) the editor draws itself.
void rv_editor_theme_apply(const rv_editor_theme &theme, ImGuiStyle &style);

} // namespace rv_editor
