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
    uint32_t code_base;     // code area
    // Text in a code area (Code, Output). Same palette as rv_editor_init.lua.
    uint32_t code_text;     // text
    uint32_t code_subtext;  // times, sources
    uint32_t code_yellow;   // warnings
    uint32_t code_red;      // errors
    uint32_t code_green;
    uint32_t code_blue;     // information
    uint32_t code_magenta;  // pink, cyan (teal) and others: the rest of a terminal's ANSI colours
    uint32_t code_cyan;
    uint32_t code_surface;
    uint32_t error;
    uint32_t warning;
    uint32_t ok;
    int32_t bevel_px;       // bevel edge width, before scale
    int32_t pad_px;         // inner padding, before scale
    float scale;            // whole UI scale, from --scale and View > UI Scale: the font is a pixel font
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
    .code_base = 0x1d2119,
    .code_text = 0xd8ded3,
    .code_subtext = 0xa3ac97,
    .code_yellow = 0xd8c36a,
    .code_red = 0xd9776b,
    .code_green = 0xa3bf6e,
    .code_blue = 0x7da3c4,
    .code_magenta = 0xc99bb0,
    .code_cyan = 0x79b8a4,
    .code_surface = 0x59634f,
    .error = 0xda4453,
    .warning = 0xf67400,
    .ok = 0x27ae60,
    .bevel_px = 2, // Motif: a two-pixel shadow on every raised or sunken edge
    .pad_px = 4,
    .scale = 1,
};

} // namespace rv_editor
