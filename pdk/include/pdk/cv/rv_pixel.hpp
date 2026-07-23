#pragma once

#include <cstdint>

namespace rv_3dmppc {

// DISPLAY pixel format — how a pixel is stored in the framebuffer that is scanned
// out to the screen. This is NOT a texture format: textures use rv_texfmt, and
// there is no 24-bit texture. The 24-bit form exists only for the video-output
// path (direct VRAM transfers / movie playback), never for drawing commands.
//
// DEFERRED: nothing in the drawing surface uses this yet; it belongs to the
// video-output stage and is kept here as a placeholder.
enum rv_pixel_type {
    RV_PIXEL_TYPE_DD_15 = 1,  // 15-bit direct colour (+ mask bit)
    RV_PIXEL_TYPE_DD_24 = 2,  // 24-bit direct colour (transfers / video only)
};

struct rv_pixel {
    union {
        uint16_t dd_15;
        uint32_t dd_24;
    } dd;
};

}  // namespace rv_3dmppc
