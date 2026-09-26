#pragma once

#include "imgui.h"

namespace rv_editor
{

// The editor's fonts (docs/adr/0004-fonts.md): the interface draws in Liberation
// Sans, pane titles in its italic; code and logs in PxPlus in one of three sizes.
// Every size is multiplied by the UI scale.
inline constexpr float rv_editor_font_ui_px = 14.0f;

// small: PxPlus IBM EGA 8x14 at 14 px; normal and large: PxPlus IBM VGA 9x16 at
// 16 and 32 px.
enum class rv_editor_code_size
{
    small,
    normal,
    large,
};

// Adds the fonts; the interface font becomes ImGui's default. False when the
// interface font or both code fonts do not load; a missing italic draws titles
// upright, a missing code font leaves the other's sizes, and each says so.
bool rv_editor_fonts_add(ImFontAtlas &atlas, float scale);

ImFont *rv_editor_font_ui();
// The interface font's italic, for pane and dialog titles.
ImFont *rv_editor_font_ui_italic();

// The code size Code and Output draw in; a size whose font did not load takes
// the other font.
void rv_editor_font_code_size_set(rv_editor_code_size size);
rv_editor_code_size rv_editor_font_code_size();
// "small", "normal", "large", as the view file and the menu name them.
const char *rv_editor_code_size_name(rv_editor_code_size size);
bool rv_editor_code_size_parse(const char *name, rv_editor_code_size &size);

// Draws what follows in the code font, until rv_editor_font_code_pop().
void rv_editor_font_code_push();
void rv_editor_font_code_pop();

} // namespace rv_editor
