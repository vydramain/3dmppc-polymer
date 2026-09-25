#pragma once

#include "imgui.h"

namespace rv_editor
{

// Cell sizes of the editor's two fonts (docs/adr/0004-fonts.md): the interface
// draws in pdklib's 5x7 font in an 8x8 cell, code and logs in PxPlus IBM VGA 9x16.
inline constexpr int rv_editor_font_ui_height = 8;
inline constexpr int rv_editor_font_code_height = 16;

// Adds both fonts at their cell height times `scale`; the interface font becomes
// ImGui's default. False when the code font file cannot be read.
bool rv_editor_fonts_add(ImFontAtlas &atlas, int scale);

ImFont *rv_editor_font_ui();

// Draws what follows in the code font, until rv_editor_font_code_pop().
void rv_editor_font_code_push();
void rv_editor_font_code_pop();

} // namespace rv_editor
