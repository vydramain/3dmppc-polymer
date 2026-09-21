#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "pdk/cv/rv_texture.h"

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
    rv_texfmt format = RV_TEXFMT_DIRECT15;
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
    case RV_TEXFMT_IDX4:
        return ((header.width + 1) / 2) * header.height;
    case RV_TEXFMT_IDX8:
        return header.width * header.height;
    default:
        return header.width * header.height * 2;
    }
}

/// Bytes one palette entry occupies. Every entry is a 16-bit colour, the same
/// encoding a DIRECT15 texel uses.
inline constexpr int64_t rv_mppctex_palette_entry_bytes = 2;

namespace detail {
inline uint16_t rv_mppctex_read_le16(const std::byte *p)
{
    return static_cast<uint16_t>(static_cast<unsigned>(std::to_integer<uint8_t>(p[0])) |
                                  (static_cast<unsigned>(std::to_integer<uint8_t>(p[1])) << 8));
}
} // namespace detail

// --- the ONE parse+validate, and the ONE write ---
//
// Four places read or write this container: mppcbaker writes it, mppcburner
// reads one back to check it against [budget] before it goes on a disc, a
// disc's own loader (rv_pccd_fs::texture_decode_) reads it to upload the
// texture, and a hot-reload does the same read again for a changed file. A
// rule enforced by only one of those four is not enforced - it is a rule the
// OTHER three trust without checking, which is exactly how the console and
// the burner drifted: the console refused a zero-width texture and a
// truncated payload, the burner did not, so a texture the burner would bake
// and pass could still be a texture the console refuses at load. This
// function is the strictest of what the three call sites used to check
// separately, so passing it once is passing it everywhere.
//
// It is `bool` + `std::string &error` because that is how the rest of
// pdklib already reports a failed parse (see rv_disc_hash_compute() and
// rv_disc_hash_magic_offset()): a caller that only needs pass/fail (the
// console, which turns any `false` into RV_ERR_INVAL) reads the return
// value and nothing else; a caller that prints for a human (the burner)
// reads `error` too.

/// Parses and fully validates one .mppctex header out of `bytes`, which must
/// hold the header AND the palette AND the texels it declares - a buffer that
/// is merely long enough for the header is not long enough to pass.
///
/// On success, fills `header_out` and points `palette_out` / `texels_out`
/// INTO `bytes` (nothing is copied; `bytes` must outlive them). `palette_out`
/// is null exactly when the format carries no palette (DIRECT15).
///
/// On failure, returns false, leaves the out-parameters unspecified, and sets
/// `error` to one sentence naming which rule the container broke.
inline bool rv_mppctex_parse(std::span<const std::byte> bytes, rv_mppctex_header &header_out,
                              const std::byte *&palette_out, const std::byte *&texels_out,
                              std::string &error)
{
    if (static_cast<int64_t>(bytes.size()) < rv_mppctex_header_size) {
        error = "is shorter than its own header";
        return false;
    }

    const std::byte *raw = bytes.data();
    bool magic_ok = true;
    for (int i = 0; i < static_cast<int>(sizeof(rv_mppctex_magic)); ++i) {
        if (raw[RV_MPPCTEX_OFF_MAGIC + i] != static_cast<std::byte>(rv_mppctex_magic[i])) {
            magic_ok = false;
            break;
        }
    }
    if (!magic_ok) {
        error = "has no MPTX magic";
        return false;
    }

    const uint16_t version = detail::rv_mppctex_read_le16(raw + RV_MPPCTEX_OFF_VERSION);
    if (version != rv_mppctex_version) {
        error = "is container version " + std::to_string(version) + ", not the version " +
                std::to_string(rv_mppctex_version) + " this code reads";
        return false;
    }

    rv_mppctex_header header;
    header.format = static_cast<rv_texfmt>(detail::rv_mppctex_read_le16(raw + RV_MPPCTEX_OFF_FORMAT));
    header.width = detail::rv_mppctex_read_le16(raw + RV_MPPCTEX_OFF_WIDTH);
    header.height = detail::rv_mppctex_read_le16(raw + RV_MPPCTEX_OFF_HEIGHT);
    header.palette_count = detail::rv_mppctex_read_le16(raw + RV_MPPCTEX_OFF_PALETTE_COUNT);

    // The FORMAT decides whether a palette is required, and how big it may
    // be. Counting bytes alone is not enough: an IDX8 whose palette was
    // deleted and whose palette_count was zeroed has exactly the byte count
    // its header promises. A paletted texture with no palette is not a
    // texture.
    switch (header.format) {
    case RV_TEXFMT_IDX4:
        if (header.palette_count == 0 || header.palette_count > 16) {
            error = "declares an IDX4 palette of " + std::to_string(header.palette_count) +
                    " entries, not 1..16";
            return false;
        }
        break;
    case RV_TEXFMT_IDX8:
        if (header.palette_count == 0 || header.palette_count > 256) {
            error = "declares an IDX8 palette of " + std::to_string(header.palette_count) +
                    " entries, not 1..256";
            return false;
        }
        break;
    case RV_TEXFMT_DIRECT15:
        // A direct texture samples no palette, so one here is a header
        // describing something no reader of this container can draw.
        if (header.palette_count != 0) {
            error = "is DIRECT15 but declares a " + std::to_string(header.palette_count) +
                    "-entry palette";
            return false;
        }
        break;
    default:
        error = "claims unknown format " + std::to_string(static_cast<int>(header.format));
        return false;
    }

    // Zero of either dimension uploads nothing and draws nothing; it is a
    // corrupt header, not an empty picture.
    if (header.width == 0 || header.height == 0) {
        error = "has a zero width or height";
        return false;
    }

    const int64_t palette_bytes = header.palette_count * rv_mppctex_palette_entry_bytes;
    const int64_t texel_bytes = rv_mppctex_texel_bytes(header);
    const int64_t need = rv_mppctex_header_size + palette_bytes + texel_bytes;
    if (static_cast<int64_t>(bytes.size()) < need) {
        error = "is " + std::to_string(bytes.size()) + " bytes, short of the " + std::to_string(need) +
                " its header, palette and texels require";
        return false;
    }

    header_out = header;
    palette_out = header.palette_count > 0 ? raw + rv_mppctex_header_size : nullptr;
    texels_out = raw + rv_mppctex_header_size + palette_bytes;
    return true;
}

/// Appends the 16-byte header for a texture of `format`, `width` x `height`
/// with `palette_count` palette entries (0 for DIRECT15) to `*out`. This is
/// the write-side counterpart of rv_mppctex_parse(): mppcbaker calls it once
/// per texture instead of spelling the byte layout out itself, which is what
/// let the two drift in the first place.
inline void rv_mppctex_write_header(rv_texfmt format, int64_t width, int64_t height,
                                     uint16_t palette_count, std::vector<uint8_t> *out)
{
    const auto put_u16 = [out](uint16_t v) {
        out->push_back(static_cast<uint8_t>(v & 0xFF));
        out->push_back(static_cast<uint8_t>(v >> 8));
    };
    out->insert(out->end(), rv_mppctex_magic, rv_mppctex_magic + sizeof(rv_mppctex_magic));
    put_u16(rv_mppctex_version);
    put_u16(static_cast<uint16_t>(format));
    put_u16(static_cast<uint16_t>(width));
    put_u16(static_cast<uint16_t>(height));
    put_u16(palette_count);
    put_u16(0); // reserved
}

} // namespace rv_pdklib
