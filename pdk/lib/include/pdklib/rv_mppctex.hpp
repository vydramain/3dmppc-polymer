#pragma once

#include <cstdint>

#include "pdk/cv/rv_texture.hpp"

namespace rv_pdklib
{

// --- the .mppctex container ---
//
// One baked texture: a 16-byte header, then the palette when the format has
// one, then the texels. Little-endian throughout.
//
// This header is the ONE description of that layout. mppcbaker writes by it,
// mppcburner reads it back to check a disc's [budget], and a disc reads it to
// upload texels — three programs that would otherwise each restate the offsets
// and drift apart the first time the format changes.
//
// The header is 16 bytes so the palette that follows is 2-byte aligned in the
// file and a disc may point a `const uint16_t *` straight at it after one read.

/// Bytes of the fixed header, palette and texels excluded.
inline constexpr int rv_mppctex_header_size = 16;

/// Container version this layout describes. It is the CONTAINER's version, not
/// the texture's and not the console's: it changes only when these offsets
/// change, which is what lets a reader refuse a file it would misparse.
inline constexpr uint16_t rv_mppctex_version = 1;

/// Byte offsets inside the header.
enum rv_mppctex_offset : int {
    RV_MPPCTEX_OFF_MAGIC = 0,         ///< 4 bytes, "MPTX"
    RV_MPPCTEX_OFF_VERSION = 4,       ///< uint16, rv_mppctex_version
    RV_MPPCTEX_OFF_FORMAT = 6,        ///< uint16, an rv_texfmt enumerator
    RV_MPPCTEX_OFF_WIDTH = 8,         ///< uint16, texels
    RV_MPPCTEX_OFF_HEIGHT = 10,       ///< uint16, texels
    RV_MPPCTEX_OFF_PALETTE_COUNT = 12 ///< uint16, entries; 0 for DIRECT15
};

/// The four magic bytes a .mppctex opens with.
inline constexpr char rv_mppctex_magic[4] = { 'M', 'P', 'T', 'X' };

/// One header, already decoded.
struct rv_mppctex_header {
    rv_pdk::rv_texfmt format = rv_pdk::RV_TEXFMT_DIRECT15;
    int64_t width = 0;
    int64_t height = 0;
    int64_t palette_count = 0;
};

/// Bytes the texels of @p header occupy, by the strides each format defines.
///
/// IDX4 packs two texels per byte and pads each ROW, not the image, so an odd
/// width costs half a byte per row rather than half a byte in total.
///
/// @param header  a decoded header
/// @return the texel payload size in bytes, palette excluded
inline int64_t rv_mppctex_texel_bytes(const rv_mppctex_header &header)
{
    switch (header.format) {
    case rv_pdk::RV_TEXFMT_IDX4:
        return ((header.width + 1) / 2) * header.height;
    case rv_pdk::RV_TEXFMT_IDX8:
        return header.width * header.height;
    default:
        return header.width * header.height * 2;
    }
}

/// Bytes one palette entry occupies. Every entry is a 16-bit colour, the same
/// encoding a DIRECT15 texel uses.
inline constexpr int64_t rv_mppctex_palette_entry_bytes = 2;

} // namespace rv_pdklib
