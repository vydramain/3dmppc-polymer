// The one file of the editor that includes imgui_internal.h: ImFontLoader and
// the atlas packing calls are not part of ImGui's stable API, so an ImGui
// update that changes them breaks exactly here.

#include "font/rv_editor_font.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "imgui_internal.h"

#include "pdklib/rv_font/rv_font_data.hpp"

namespace rv_editor
{

namespace
{

// Whole-number magnification that fits `size` pixels into one 8-pixel cell.
int rv_editor_font_scale(float size)
{
    return std::max(1, static_cast<int>(std::floor(size / rv_pdklib::rv_font_cell_height)));
}

int rv_editor_font_glyph_index(ImWchar codepoint)
{
    if (codepoint < rv_pdklib::rv_font_first_code || codepoint > rv_pdklib::rv_font_last_code) {
        return rv_pdklib::rv_font_notdef_index;
    }
    return static_cast<int>(codepoint) - rv_pdklib::rv_font_first_code;
}

bool rv_editor_font_contains(ImFontAtlas *, ImFontConfig *, ImWchar)
{
    return true;
}

bool rv_editor_font_baked_init(ImFontAtlas *, ImFontConfig *, ImFontBaked *baked, void *)
{
    const int k = rv_editor_font_scale(baked->Size);
    baked->Ascent = static_cast<float>(rv_pdklib::rv_font_ink_height * k);
    baked->Descent = -static_cast<float>((rv_pdklib::rv_font_cell_height - rv_pdklib::rv_font_ink_height) * k);
    return true;
}

bool rv_editor_font_load_glyph(ImFontAtlas *atlas, ImFontConfig *src, ImFontBaked *baked, void *, ImWchar codepoint,
    ImFontGlyph *out_glyph, float *out_advance_x)
{
    const int k = rv_editor_font_scale(baked->Size);
    const float advance = static_cast<float>(rv_pdklib::rv_font_cell_width * k);
    if (out_advance_x != nullptr) {
        *out_advance_x = advance;
        return true;
    }

    out_glyph->Codepoint = codepoint;
    out_glyph->AdvanceX = advance;

    const uint8_t *rows = &rv_pdklib::rv_font_bits[rv_editor_font_glyph_index(codepoint) * rv_pdklib::rv_font_cell_height];
    bool ink = false;
    for (int row = 0; row < rv_pdklib::rv_font_cell_height; ++row) {
        ink = ink || rows[row] != 0;
    }
    if (!ink) {
        return true;
    }

    const int w = rv_pdklib::rv_font_cell_width * k;
    const int h = rv_pdklib::rv_font_cell_height * k;
    const ImFontAtlasRectId pack_id = ImFontAtlasPackAddRect(atlas, w, h);
    if (pack_id == ImFontAtlasRectId_Invalid) {
        return false;
    }
    ImTextureRect *r = ImFontAtlasPackGetRect(atlas, pack_id);

    // Row bytes are top row first; the high bit is the leftmost pixel.
    std::vector<unsigned char> pixels(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t bits = rows[y / k];
        for (int x = 0; x < w; ++x) {
            if ((bits & (0x80u >> (x / k))) != 0) {
                pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = 0xff;
            }
        }
    }

    out_glyph->X0 = 0.0f;
    out_glyph->Y0 = 0.0f;
    out_glyph->X1 = static_cast<float>(w);
    out_glyph->Y1 = static_cast<float>(h);
    out_glyph->Visible = true;
    out_glyph->PackId = pack_id;
    ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out_glyph, r, pixels.data(), ImTextureFormat_Alpha8, w);
    return true;
}

ImFontLoader rv_editor_font_loader_make()
{
    ImFontLoader loader;
    loader.Name = "rv_font";
    loader.FontSrcContainsGlyph = rv_editor_font_contains;
    loader.FontBakedInit = rv_editor_font_baked_init;
    loader.FontBakedLoadGlyph = rv_editor_font_load_glyph;
    return loader;
}

const ImFontLoader rv_editor_font_loader = rv_editor_font_loader_make();

} // namespace

ImFont *rv_editor_font_add(ImFontAtlas &atlas, int scale)
{
    ImFontConfig config;
    config.FontLoader = &rv_editor_font_loader;
    config.SizePixels = static_cast<float>(rv_pdklib::rv_font_cell_height * std::max(1, scale));
    config.PixelSnapH = true;
    std::strncpy(config.Name, "rv_font 8x8", sizeof(config.Name) - 1);
    return atlas.AddFont(&config);
}

} // namespace rv_editor
