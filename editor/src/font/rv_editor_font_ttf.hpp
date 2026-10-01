#pragma once

#include <string>

namespace rv_editor
{

// Advance of the interface font: five columns of ink and one of space.
inline constexpr int rv_editor_font_ui_advance = 6;

// pdklib's 5x7 font (rv_font_data.hpp) with its Cyrillic block, as the bytes of
// a TrueType file: 8 px high, ascent the whole cell, every advance 6 px. `scale` is target pixels per
// source pixel; the em is round(8 * scale) px, to be loaded at that size. Source edges round to whole pixels.
std::string rv_editor_font_ttf(double scale = 1.0);

// Pixel height of the interface font's em at `scale`; the size to load rv_editor_font_ttf(scale) at.
int rv_editor_font_ttf_em(double scale);

} // namespace rv_editor
