#pragma once

#include "imgui.h"

namespace rv_editor
{

// Adds pdklib's 8x8 bitmap font (pdklib/rv_font/rv_font_data.hpp) to an ImGui
// atlas at 8 * scale pixels. Glyphs are produced by the editor's own
// ImFontLoader: each font pixel becomes an N x N block for the whole-number N
// that fits the requested size, so the atlas holds 0/255 alpha only and text is
// never smoothed.
//
// A code point outside ASCII 32..126 draws as the font's notdef block.
ImFont *rv_editor_font_add(ImFontAtlas &atlas, int scale);

} // namespace rv_editor
