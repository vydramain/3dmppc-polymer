#pragma once

#include <cstddef>
#include <cstdint>

#include "pdk/cv/rv_texture.h"
#include "pdklib/rv_font/rv_font_data.hpp"
#include "pdklib/rv_textures/rv_texel_pack.hpp"

namespace rv_pdklib
{

// Font atlas and palette: glyph geometry and the textures a disc uploads once.

// --- the atlas ----------------------------------------------------------------

// 16 cells across, 6 down = 96 cells for the font's 96 glyphs (95 printable plus
// the notdef block, which is the last cell). 16 across is not arbitrary: a power
// of two makes the cell arithmetic a shift and a mask, and it makes the atlas 128
// texels wide, which is a size any plausible texture limit accepts.
inline constexpr int rv_font_atlas_columns = 16;
inline constexpr int rv_font_atlas_rows = 6;

inline constexpr int rv_font_atlas_width = rv_font_atlas_columns * rv_font_cell_width; // 128
inline constexpr int rv_font_atlas_height = rv_font_atlas_rows * rv_font_cell_height;  // 48

// IDX4 stores two texels per byte, so a row of 128 texels is 64 bytes.
inline constexpr std::size_t rv_font_atlas_stride =
    static_cast<std::size_t>(rv_font_atlas_width) / 2;
inline constexpr std::size_t rv_font_atlas_size =
    rv_font_atlas_stride * static_cast<std::size_t>(rv_font_atlas_height); // 3072 bytes

// The two palette indices the atlas uses. Everything else in the 16-entry palette
// stays 0000h, i.e. transparent, so a corrupted index draws nothing rather than a
// stray colour.
inline constexpr uint8_t rv_font_index_background = 0;
inline constexpr uint8_t rv_font_index_ink = 1;

inline constexpr std::size_t rv_font_palette_entries = 16;                       // IDX4 palette
inline constexpr std::size_t rv_font_palette_size = rv_font_palette_entries * 2; // bytes

// Map a byte to a cell index. Anything outside the printable ASCII range — a
// control byte, a stray 0x0D from CRLF line endings, the lead byte of a UTF-8
// sequence — lands on the notdef block on purpose: broken encoding must look
// broken, not empty.
inline int rv_font_glyph_index(char c)
{
    const unsigned int code = static_cast<unsigned char>(c);
    if (code < static_cast<unsigned int>(rv_font_first_code) ||
        code > static_cast<unsigned int>(rv_font_last_code)) {
        return rv_font_notdef_index;
    }
    return static_cast<int>(code) - rv_font_first_code;
}

// THEOREM: character -> cell -> texel coordinates. A glyph's index is `code - 32`
// (the notdef block is index 95), the atlas is filled left to right and top to
// bottom, so index i sits in column i % 16 and row i / 16, and its upper-left
// texel is
//
//     u0 = (i % 16) * 8        v0 = (i / 16) * 8
//
// The cell then covers the HALF-OPEN texel range [u0, u0 + 8) x [v0, v0 + 8).
// Half-open is the load-bearing word, and it is what the quad's uv are built from
// below: the right and bottom edges belong to the NEXT cell and are never
// sampled, which is what stops a glyph from smearing one column of its neighbour
// along its edge. Cell width 8 also keeps u0 EVEN, so every cell begins on a whole
// byte in the IDX4 packing — a glyph never straddles a nibble boundary.
inline int rv_font_cell_u(int glyph_index)
{
    return (glyph_index % rv_font_atlas_columns) * rv_font_cell_width;
}

inline int rv_font_cell_v(int glyph_index)
{
    return (glyph_index / rv_font_atlas_columns) * rv_font_cell_height;
}

// Expand the bitmap font into IDX4 texels, ready for rv_cv_video_asset_write.
//
// The buffer belongs to the CALLER — this header never allocates and never talks
// to the console. pdklib/ has no rv_cv to talk to: it is built over the contract, and
// the disc is the one holding the machine. Typical use is a 3 KiB array the disc
// keeps on the stack for the length of disc_initialize and drops afterwards; the
// bytes are copied during the upload.
//
// Returns false if `out` is null or `size` is under rv_font_atlas_size.
//
// THEOREM: IDX4 packing. Two texels share a byte and the LOW nibble is the LEFT
// one — the PSX order, fixed by the console (the
// sampler in src/.../rv_pctexel.cpp reads it as `(u & 1) ? packed >> 4 : packed &
// 0x0F`). A converter has to agree with exactly one convention and this is it.
// Rows are padded to a whole byte, so the row stride is (width + 1) / 2 rather
// than width / 2; at width 128 the two agree, but the formula is what the console
// uses and copying it here keeps an odd-width atlas from shearing if these numbers
// are ever changed. Texel (u, v) therefore lives at byte v * stride + u / 2, in
// the low nibble when u is even and the high nibble when u is odd.
inline bool rv_font_build_atlas(uint8_t *out, std::size_t size)
{
    if (out == nullptr || size < rv_font_atlas_size) {
        return false;
    }

    // Index 0 everywhere: the background, which the palette makes transparent.
    for (std::size_t i = 0; i < rv_font_atlas_size; ++i) {
        out[i] = 0;
    }

    for (int glyph = 0; glyph < rv_font_glyph_count; ++glyph) {
        const int cell_u = rv_font_cell_u(glyph);
        const int cell_v = rv_font_cell_v(glyph);

        for (int row = 0; row < rv_font_cell_height; ++row) {
            const unsigned int bits = rv_font_bits[glyph * rv_font_cell_height + row];
            if (bits == 0) {
                continue; // a blank row touches no byte
            }

            const std::size_t base = static_cast<std::size_t>(cell_v + row) * rv_font_atlas_stride;

            for (int col = 0; col < rv_font_cell_width; ++col) {
                // The font's high bit is the leftmost pixel (rv_font_data.hpp).
                if ((bits & (0x80U >> col)) == 0) {
                    continue;
                }

                const int u = cell_u + col;
                uint8_t &packed = out[base + static_cast<std::size_t>(u / 2)];
                const unsigned int nibble = (u & 1) != 0 ? static_cast<unsigned int>(rv_font_index_ink) << 4 : static_cast<unsigned int>(rv_font_index_ink);
                packed = static_cast<uint8_t>(packed | nibble);
            }
        }
    }
    return true;
}

// Describe an atlas buffer for rv_cv_video_asset_write. The rv_texture
// only BORROWS the bytes (pdk/cv/rv_texture.h), so `data` must outlive the call.
inline rv_texture rv_font_atlas_texture(const uint8_t *data)
{
    rv_texture texture{};
    texture.format = RV_TEXFMT_IDX4;
    texture.data = data;
    texture.size = rv_font_atlas_size;
    texture.width = static_cast<uint64_t>(rv_font_atlas_width);
    texture.height = static_cast<uint64_t>(rv_font_atlas_height);
    return texture;
}

// --- the palette --------------------------------------------------------------

// Pack an rv_color into one palette entry. Truncation and not rounding:
// it is the same conversion the framebuffer performs, so what a disc asks for is
// what it gets.
//
// THE BLACK TRAP, and why rv_texel_opaque is not optional here: 0000h is not
// black, it is FULLY TRANSPARENT. An ink colour of pure black would punch the
// glyph out of the picture and the string would silently disappear — the exact
// failure this whole header exists to make impossible.
inline uint16_t rv_font_pack_rgb555(rv_color c)
{
    return rv_texel_opaque(rv_texel_pack(rv_texel_truncate(c)));
}

// Build the 16-entry palette a text atlas is drawn with: entry 0 transparent
// (the glyph background), entry 1 the ink, the remaining 14 transparent.
//
// One colour of text = one of these, uploaded once. A disc that wants white body
// text, a yellow highlight and a red warning uploads three palettes of 32 bytes
// each and switches rv_polygon::addr_palette — see the PATTERN at the top.
//
// Returns false if `out` is null or `count` is under rv_font_palette_entries.
inline bool rv_font_build_palette(rv_color ink, uint16_t *out, std::size_t count)
{
    if (out == nullptr || count < rv_font_palette_entries) {
        return false;
    }
    for (std::size_t i = 0; i < rv_font_palette_entries; ++i) {
        out[i] = 0x0000;
    }
    out[rv_font_index_ink] = rv_font_pack_rgb555(ink);
    return true;
}

// Describe a palette buffer for rv_cv_video_asset_write. A palette is uploaded as
// a DIRECT15 texture of `entries` x 1 — the contract says so explicitly, because a
// palette entry and a DIRECT15 texel are the same 16-bit value.
inline rv_texture rv_font_palette_texture(const uint16_t *entries)
{
    rv_texture texture{};
    texture.format = RV_TEXFMT_DIRECT15;
    texture.data = entries;
    texture.size = rv_font_palette_size;
    texture.width = static_cast<uint64_t>(rv_font_palette_entries);
    texture.height = 1;
    return texture;
}

} // namespace rv_pdklib
