#pragma once

#include <cstdint>

namespace rv_pdklib
{

// Olive theme shared by the console start screen and the built-in disc. Source:
// rv_editor_theme_olive in editor/src/theme/rv_editor_theme.hpp; keep the two equal.

// Colours, 0xRRGGBB.
inline constexpr uint32_t rv_theme_window = 0x4c5844;
inline constexpr uint32_t rv_theme_inset = 0x3e4637;
inline constexpr uint32_t rv_theme_button = 0x4e5744;
inline constexpr uint32_t rv_theme_dark = 0x282e20;
inline constexpr uint32_t rv_theme_bevel_hi = 0x7e8776;
inline constexpr uint32_t rv_theme_bevel_lo = 0x32392c;
inline constexpr uint32_t rv_theme_text = 0xd8ded3;
inline constexpr uint32_t rv_theme_text_bright = 0xf1f2f0;
inline constexpr uint32_t rv_theme_text_disabled = 0x758666;
inline constexpr uint32_t rv_theme_warning = 0xf67400;
inline constexpr uint32_t rv_theme_error = 0xda4453;

// Sizes in pixels at a 320x240 frame.

// Bevel width: light edge top and left on a raised plate, the reverse on a sunken one.
inline constexpr int32_t rv_theme_bevel_px = 2;
// Window gap from the frame edge, horizontal and vertical.
inline constexpr int32_t rv_theme_window_margin_x = 8;
inline constexpr int32_t rv_theme_window_margin_y = 6;
// Title bar height; the bar starts inside the window bevel, at window top + bevel.
inline constexpr int32_t rv_theme_title_h = 16;
// Title text offset from the window's left and top edges.
inline constexpr int32_t rv_theme_title_text_x = 8;
inline constexpr int32_t rv_theme_title_text_y = 5;
// Content inset from the window's left edge; content width = window width - 2 * inset.
inline constexpr int32_t rv_theme_content_x = 8;
// Text line pitch.
inline constexpr int32_t rv_theme_line_pitch = 8;
// Button height, label padding on each side, and label top offset from the button top.
inline constexpr int32_t rv_theme_button_h = 16;
inline constexpr int32_t rv_theme_button_pad_x = 8;
inline constexpr int32_t rv_theme_button_text_y = 4;
// Top of the button band, measured up from the window's bottom edge.
inline constexpr int32_t rv_theme_button_band_above_bottom = 26;
// Text inset from a sunken panel's left edge.
inline constexpr int32_t rv_theme_panel_text_x = 6;

} // namespace rv_pdklib
