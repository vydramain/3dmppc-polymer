#pragma once

#include "imgui.h"

namespace rv_editor
{

// The interface uses an 8 px baseline line height before UI scaling.
inline constexpr int rv_editor_font_ui_height = 8;

// small: PxPlus IBM EGA 8x14 at 14 px; normal and large: PxPlus IBM VGA 9x16 at
// 16 and 32 px. Each is multiplied by --scale.
enum class rv_editor_code_size {
    small,
    normal,
    large,
};

enum class rv_editor_main_size {
    normal,
    large,
};

// "normal" and "large", as stored in the view file.
const char *rv_editor_main_size_name(rv_editor_main_size size);
int rv_editor_main_size_parse(const char *name, rv_editor_main_size &size);

// Adds the fonts for UI scale 1, 1.5 or 2. Main size affects only the interface font,
// which is rebuilt on the target pixel grid and becomes ImGui's default.
// Returns RV_OK, or RV_ERR_NOENT when the interface font or both code fonts fail to load.
int rv_editor_fonts_add(ImFontAtlas &atlas, float scale, rv_editor_main_size main_size = rv_editor_main_size::normal);

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
