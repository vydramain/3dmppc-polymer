#pragma once

#include <string>

namespace rv_editor
{

// Advance of the interface font: five columns of ink and one of space.
inline constexpr int rv_editor_font_ui_advance = 6;

// The interface bitmap glyphs as a TrueType file. The em is round(8 * scale) px.
// Fractional grids at and above 1.5 use the dedicated fractional glyphs as their source.
// Source edges round to whole pixels.
std::string rv_editor_font_ttf(double scale = 1.0);

// Pixel height of the interface font's em at `scale`; the size to load rv_editor_font_ttf(scale) at.
int rv_editor_font_ttf_em(double scale);

} // namespace rv_editor
