#pragma once

#include "imgui.h"

namespace rv_editor
{

// The editor's fonts: the interface draws in pdklib's
// 5x7 font in an 8 px line; code and logs in one of three code sizes.
inline constexpr int rv_editor_font_ui_height = 8;

// small: PxPlus IBM EGA 8x14 at 14 px; normal and large: PxPlus IBM VGA 9x16 at
// 16 and 32 px. Each is multiplied by --scale.
enum class rv_editor_code_size {
    small,
    normal,
    large,
};

// Adds the fonts at their heights times `scale` (1, 1.5, 2; code sizes round to whole pixels, the
// interface font is rebuilt on the scaled pixel grid so it stays unsmoothed); the interface font becomes
// ImGui's default. Returns RV_OK on success or RV_ERR_NOENT when ImGui refuses the interface font or both code
// fonts; a code font file that does not load leaves the other's sizes, and says so.
int rv_editor_fonts_add(ImFontAtlas &atlas, float scale);

// The scale the code sizes are multiplied by, after View > UI Scale.
void rv_editor_font_scale_set(float scale);

ImFont *rv_editor_font_ui();

// The code size Code and Output draw in; a size whose font did not load takes
// the other font.
void rv_editor_font_code_size_set(rv_editor_code_size size);
rv_editor_code_size rv_editor_font_code_size();
// "small", "normal", "large", as the view file and the menu name them.
const char *rv_editor_code_size_name(rv_editor_code_size size);
int rv_editor_code_size_parse(const char *name, rv_editor_code_size &size);

// Draws what follows in the code font, until rv_editor_font_code_pop().
void rv_editor_font_code_push();
void rv_editor_font_code_pop();

} // namespace rv_editor
