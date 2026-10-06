#pragma once

#include <cstdint>

#include "imgui.h"

#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// RGB channel extraction from 0xRRGGBB packed color.
constexpr int channel_shift_red = 16;        // red channel bit position
constexpr int channel_shift_green = 8;       // green channel bit position
constexpr uint32_t rgb_channel_mask = 0xffu; // 8-bit channel mask
constexpr uint32_t alpha_opaque = 0xff;      // fully opaque alpha value

// 0xRRGGBB token -> opaque ImGui colour.
constexpr ImU32 rv_editor_col(uint32_t rgb)
{
    return IM_COL32((rgb >> channel_shift_red) & rgb_channel_mask,
        (rgb >> channel_shift_green) & rgb_channel_mask, rgb & rgb_channel_mask, alpha_opaque);
}

// Writes the tokens into an ImGui style: colours, square corners, no
// antialiasing, sizes multiplied by the theme's integer scale. Stock ImGui
// widgets then draw in the palette; what a style cannot express (two-tone
// bevels, diamonds, stipple) the editor draws itself.
void rv_editor_theme_apply(const rv_editor_theme &theme, ImGuiStyle &style);

} // namespace rv_editor
