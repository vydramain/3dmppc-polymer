// mppcbaker: the .mppctex header and the IDX4/IDX8/DIRECT15 encoders.
#include "rv_baker_encode.hpp"

#include <cstdio>

#include "pdklib/rv_stdio/rv_stdio.hpp"
#include "pdklib/rv_textures/rv_texel_pack.hpp"
#include "rv_baker_quantize.hpp"

namespace {

// --- the file format ----------------------------------------------------------

// Header, little-endian, 16 bytes. Kept at 16 so the palette that follows it is
// 2-byte aligned in the file and a disc may point a `const uint16_t*` straight
// at it after a single read.
constexpr char kMagic[4] = { 'M', 'P', 'T', 'X' };
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderSize = 16;

// A palette is written at FULL length whatever the image needed — 16 entries for
// IDX4, 256 for IDX8 — so the disc uploads one fixed-shape rv_texture without
// caring how many colours the artwork used. The two sizes are what 4 and 8 index
// bits can address.
constexpr size_t kPaletteSizeIdx4 = 16;
constexpr size_t kPaletteSizeIdx8 = 256;

// PATTERN: reserved slot — index 0 is the hole whenever the image has one.
// Transparency in an indexed format lives in the PALETTE (rv_texture.h:
// "transparency is decided AFTER the palette lookup"), so a cut-out sprite
// indexes a fixed 0000h entry. It costs one colour, hence it is only reserved
// when the image actually has transparent pixels.
constexpr uint8_t kHoleIndex = 0;

// How many Lloyd passes run after median cut. A fixed count, not "until
// convergence": the gain collapses after the first few passes, and a fixed
// number keeps the tool terminating in bounded time with identical output run
// after run.
constexpr int kLloydPasses = 4;

// --- small helpers ------------------------------------------------------------

// The colour vocabulary is the console's, not this tool's: rv_color is the 8-bit
// triple a PNG pixel becomes, rv_color5 the 5-bit one everything downstream
// measures in, and rv_texel_* the layout and the quantiser both ends agree on.
// Named here so the bodies below read as they did when the tool owned them.
using rv_pdklib::rv_texel_opaque;
using rv_pdklib::rv_texel_pack;
using rv_pdklib::rv_texel_unpack;

// THEOREM: the opaque-black trap — 0000h is the transparency sentinel, so an
// OPAQUE pixel may never encode to it. Naive quantisation sends every very dark
// pixel there and the picture's shadows turn into holes. rv_texel_opaque is that
// fix, applied wherever an opaque colour becomes a 16-bit word.


// Every colour the console can express: three channels of five bits. 32768
// counters is small enough to histogram by direct indexing, which is why this
// tool needs no hash map and has no ordering ambiguity to resolve.
constexpr size_t kColor5Codes = 1u << 15;

// --- output -------------------------------------------------------------------

// Appends one little-endian uint16, the only multi-byte shape the format uses.
void put_u16(std::vector<uint8_t> &out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

// The 16 fixed bytes every .mppctex opens with. RV_ERR_INVAL here is an
// invariant violation, not a user error: the layout is documented at kHeaderSize
// and a mismatch means this function and that constant have drifted apart.
rv_err write_header(rv_texfmt format, const source_image &src, uint16_t palette_entries,
    std::vector<uint8_t> *out, baker_error *error)
{
    out->insert(out->end(), kMagic, kMagic + sizeof(kMagic));
    put_u16(*out, kVersion);
    put_u16(*out, static_cast<uint16_t>(format));
    put_u16(*out, static_cast<uint16_t>(src.width));
    put_u16(*out, static_cast<uint16_t>(src.height));
    put_u16(*out, palette_entries);
    put_u16(*out, 0); // reserved
    if (out->size() != kHeaderSize) {
        error->message = "internal: header size drifted from the documented layout";
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// DIRECT15 carries no palette: a texel is the colour itself.
void encode_direct15(const source_image &src, std::vector<uint8_t> *out)
{
    out->reserve(out->size() + src.pixels.size() * 2);
    for (const src_pixel &s : src.pixels) {
        put_u16(*out,
            s.transparent ? RV_TEXEL_TRANSPARENT : rv_texel_opaque(rv_texel_pack(s.color)));
    }
}

// One bin per distinct OPAQUE colour, with the pixel count that colour carries.
// A hole keeps whatever RGB the artist left under the alpha, and that colour
// must not drag the palette towards it.
void histogram(const source_image &src, std::vector<color_bin> *out)
{
    // Bucketed directly, one counter per code — see kColor5Codes.
    std::vector<uint32_t> counts(kColor5Codes, 0);
    for (const src_pixel &s : src.pixels) {
        if (!s.transparent) {
            ++counts[rv_texel_pack(s.color)];
        }
    }
    for (uint32_t code = 0; code < counts.size(); ++code) {
        if (counts[code] != 0) {
            out->push_back(color_bin{ rv_texel_unpack(static_cast<uint16_t>(code)), counts[code] });
        }
    }
}

// IDX4 rows, two texels to a byte — see kNibbleBits.
void pack_nibbles(const source_image &src, const std::vector<uint8_t> &indices,
    std::vector<uint8_t> *out)
{
    const size_t width = static_cast<size_t>(src.width);
    // The +1 is what pads an odd width so every row still starts on a byte.
    const size_t stride = (width + 1) / 2;
    out->reserve(out->size() + stride * static_cast<size_t>(src.height));
    for (int y = 0; y < src.height; ++y) {
        const size_t row = static_cast<size_t>(y) * width;
        for (size_t x = 0; x < width; x += 2) {
            const uint8_t low = static_cast<uint8_t>(indices[row + x] & kNibbleMask);
            const uint8_t high = (x + 1 < width) ? static_cast<uint8_t>(indices[row + x + 1] & kNibbleMask) : uint8_t{ 0 };
            out->push_back(static_cast<uint8_t>(low | (high << kNibbleBits)));
        }
    }
}

// Header, palette and indices for IDX4/IDX8. RV_ERR_INVAL is an image with
// nothing to put in a palette.
rv_err encode_indexed(const options &opt, const source_image &src, std::vector<uint8_t> *out,
    baker_error *error)
{
    const size_t palette_size = (*opt.format == RV_TEXFMT_IDX4) ? kPaletteSizeIdx4 : kPaletteSizeIdx8;
    // One slot spent on kHoleIndex, and only when the image needs a hole.
    const bool needs_hole = src.transparent_count > 0;
    const size_t reserved = needs_hole ? 1u : 0u;
    const size_t color_slots = palette_size - reserved;

    std::vector<color_bin> bins;
    histogram(src, &bins);
    if (bins.empty()) {
        error->message = "'" + opt.input + "' has no opaque pixel: there is nothing to put in a palette";
        return RV_ERR_INVAL;
    }
    if (bins.size() > color_slots) {
        rv_pdklib::rv_fprintf(stderr,
            "mppcbaker: note: %zu distinct colours reduced to %zu palette entries\n", bins.size(),
            color_slots);
    }

    std::vector<rv_color5> palette = median_cut(bins, color_slots);
    refine(bins, palette, kLloydPasses);

    // After refinement, so the indices match the palette actually written.
    std::vector<uint8_t> indices(src.pixels.size(), kHoleIndex);
    for (size_t i = 0; i < src.pixels.size(); ++i) {
        if (src.pixels[i].transparent) {
            continue; // already kHoleIndex; `reserved` is 1 whenever this is reached
        }
        indices[i] = static_cast<uint8_t>(nearest(palette, src.pixels[i].color) + reserved);
    }

    const rv_err header = write_header(*opt.format, src, static_cast<uint16_t>(palette_size), out, error);
    if (header != RV_OK) {
        return header;
    }

    // Full length always; the unused tail is 0000h, so an index that should
    // never be sampled draws nothing rather than a wrong colour.
    if (needs_hole) {
        put_u16(*out, RV_TEXEL_TRANSPARENT);
    }
    for (const rv_color5 &c : palette) {
        put_u16(*out, rv_texel_opaque(rv_texel_pack(c)));
    }
    for (size_t i = reserved + palette.size(); i < palette_size; ++i) {
        put_u16(*out, RV_TEXEL_TRANSPARENT);
    }

    if (*opt.format == RV_TEXFMT_IDX8) {
        out->insert(out->end(), indices.begin(), indices.end());
    } else {
        pack_nibbles(src, indices, out);
    }
    return RV_OK;
}

} // namespace

// The whole .mppctex, in memory. The format decides which encoder runs; both
// produce a complete file, header included.
rv_err encode_texture(const options &opt, const source_image &src, std::vector<uint8_t> *out,
    baker_error *error)
{
    if (*opt.format == RV_TEXFMT_DIRECT15) {
        const rv_err header = write_header(*opt.format, src, 0, out, error);
        if (header != RV_OK) {
            return header;
        }
        encode_direct15(src, out);
        return RV_OK;
    }
    return encode_indexed(opt, src, out, error);
}
