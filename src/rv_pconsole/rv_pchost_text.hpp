// Glyph blits for the console's own screens into a host ARGB buffer.
// Only pdklib's font DATA is borrowed: its text drawer needs the font uploaded to
// virtual VRAM, which is a disc's job, and the console must not allocate that.
#pragma once

#include <cstdint>
#include <string_view>

#include "pdklib/rv_font/rv_font_data.hpp"

namespace rv_3dmppc
{

// Row-major ARGB words, `width` per row.
struct rv_pchost_text_target {
    uint32_t *pixels;
    int64_t width;
    int64_t height;
};

// High bit of a glyph row byte: the leftmost pixel.
inline constexpr uint8_t RV_PCHOST_TEXT_ROW_MSB = 0x80u;

// One set texel, magnified to `scale` by `scale` pixels. Clipped, because a disc
// may declare a screen smaller than the text was sized for.
inline void rv_pchost_text_fill_texel(const rv_pchost_text_target &target, int64_t x0, int64_t y0, int scale, uint32_t argb)
{
    for (int sy = 0; sy < scale; ++sy) {
        for (int sx = 0; sx < scale; ++sx) {
            const int64_t px = x0 + sx;
            const int64_t py = y0 + sy;
            if (px >= 0 && px < target.width && py >= 0 && py < target.height) {
                target.pixels[py * target.width + px] = argb;
            }
        }
    }
}

// Width in pixels of `text` at `scale`, without the blank letter-spacing columns
// of the last glyph, so centring uses the ink.
inline int64_t rv_pchost_text_ink_width(std::string_view text, int scale)
{
    if (text.empty()) {
        return 0;
    }
    return static_cast<int64_t>(text.size()) * rv_pdklib::rv_font_cell_width * scale -
        (rv_pdklib::rv_font_cell_width - rv_pdklib::rv_font_ink_width) * scale;
}

// One line of `text` with its cell's top left at (x, y). Bytes outside printable
// ASCII draw the font's visible notdef cell.
inline void
rv_pchost_text_draw(const rv_pchost_text_target &target, int64_t x, int64_t y, std::string_view text, int scale, uint32_t argb)
{
    int64_t pen = x;
    for (const char c : text) {
        const int glyph = (c >= rv_pdklib::rv_font_first_code && c <= rv_pdklib::rv_font_last_code) ?
            c - rv_pdklib::rv_font_first_code :
            rv_pdklib::rv_font_notdef_index;
        const uint8_t *rows = &rv_pdklib::rv_font_bits[glyph * rv_pdklib::rv_font_cell_height];
        for (int row = 0; row < rv_pdklib::rv_font_cell_height; ++row) {
            for (int column = 0; column < rv_pdklib::rv_font_ink_width; ++column) {
                if ((rows[row] & (RV_PCHOST_TEXT_ROW_MSB >> column)) != 0) {
                    rv_pchost_text_fill_texel(target, pen + column * scale, y + row * scale, scale, argb);
                }
            }
        }
        pen += rv_pdklib::rv_font_cell_width * scale;
    }
}

} // namespace rv_3dmppc
