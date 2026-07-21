#pragma once

#include <cstdint>

namespace rv_3dmppc {

// How the texels of an uploaded texture are encoded. This is what decides whether
// a palette (CLUT) is needed: the indexed formats store a palette index per texel
// and must be drawn with rv_polygon::addr_palette set; the direct format stores
// the colour in the texel itself and ignores the palette.
//
// New formats take a new enumerator; existing values never change (they are ABI,
// like rv_err). The numeric value is the bit width of one texel, as a mnemonic.
enum rv_texfmt {
    RV_TEXFMT_IDX4 = 4,       // 4-bit palette index  (16-colour palette)
    RV_TEXFMT_IDX8 = 8,       // 8-bit palette index  (256-colour palette)
    RV_TEXFMT_DIRECT15 = 15,  // 15-bit direct colour (no palette)
};

// A block of pixel data the game hands to rv_cv::video_asset_write() for upload
// into video RAM. Plain POD: it only *borrows* the bytes (the game owns them) and
// carries its own size and dimensions, so the upload call needs no separate length
// argument.
//
// The borrow lasts for the duration of the call; the bytes are copied into video
// RAM and the game may release its buffer afterwards. A palette is uploaded the
// same way (as an array of rv_color) and referenced from a polygon by address.
struct rv_texture {
    rv_texfmt format;  // how to read `data` (and whether a palette is needed)

    const void* data;  // texel bytes, owned by the game (main RAM)
    uint64_t size;     // length of `data`, in bytes
    uint64_t width;    // texel columns
    uint64_t height;   // texel rows
};

}  // namespace rv_3dmppc
