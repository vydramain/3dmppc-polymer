#pragma once

#include "imgui.h"

namespace rv_editor
{

// Cell height of the editor's font, PxPlus IBM VGA 9x16 (third_party/pxplus-ibm-vga).
inline constexpr int rv_editor_font_height = 16;

// Adds the editor's one font, for interface and code alike, at 16 * scale pixels.
// Returns nullptr when the font file cannot be read.
ImFont *rv_editor_font_add(ImFontAtlas &atlas, int scale);

} // namespace rv_editor
