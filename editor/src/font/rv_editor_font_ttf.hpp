#pragma once

#include <string>

namespace rv_editor
{

// pdklib's 5x7 font (rv_font_data.hpp) with its Cyrillic block, as the bytes of
// a TrueType file: 8 px per cell, ascent the whole cell, every advance 8 px.
std::string rv_editor_font_ttf();
// The code font of Code and Output (rv_editor_font_code_data.hpp) the same way:
// 6x11 cell, ascent the whole cell, every advance 6 px.
std::string rv_editor_font_code_ttf();

} // namespace rv_editor
