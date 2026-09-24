#pragma once

#include <cstdint>

namespace rv_editor
{

// The editor's design tokens: plain data, no ImGui. Colours are 0xRRGGBB.
// The palette is the token table of the requirements (section 11) plus the
// state colours of vgui2-deck; rv_editor_theme_imgui.hpp maps it onto ImGui.
struct rv_editor_theme
{
    uint32_t window;        // main surface
    uint32_t inset;         // fields and sunken areas
    uint32_t button;
    uint32_t selection;     // brass: selection, focus, hover outline
    uint32_t bevel_hi;      // top and left edge of a raised frame
    uint32_t bevel_lo;      // bottom and right edge of a raised frame
    uint32_t text;
    uint32_t text_bright;
    uint32_t text_disabled;
    uint32_t dark;          // title bars, the deepest surface
    uint32_t code_base;     // code area, Catppuccin Mocha base
    uint32_t error;
    uint32_t warning;
    uint32_t ok;
    int32_t bevel_px;       // bevel edge width, before scale
    int32_t pad_px;         // inner padding, before scale
    int32_t scale;          // integer UI scale, from --scale: the font is a pixel font
};

inline constexpr rv_editor_theme rv_editor_theme_olive = {
    .window = 0x4c5844,
    .inset = 0x3e4637,
    .button = 0x4e5744,
    .selection = 0x958831,
    .bevel_hi = 0x7e8776,
    .bevel_lo = 0x32392c,
    .text = 0xd8ded3,
    .text_bright = 0xf1f2f0,
    .text_disabled = 0x758666,
    .dark = 0x282e20,
    .code_base = 0x1e1e2e,
    .error = 0xda4453,
    .warning = 0xf67400,
    .ok = 0x27ae60,
    .bevel_px = 1,
    .pad_px = 4,
    .scale = 1,
};

} // namespace rv_editor
