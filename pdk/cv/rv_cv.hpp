#pragma once

#include <cstdint>

#include "rv_pipeline.hpp"  // IWYU pragma: keep (frame_configure flag vocabulary)
#include "rv_primitives.hpp"
#include "rv_texture.hpp"
#include "rv_vertex.hpp"

namespace rv_3dmppc {

// Controller Video — the low-level GPU contract (the "hardware").
//
// Models a PSX-like GPU with two responsibilities that mirror the audio chip:
//   * video RAM — the game uploads texture / palette data into a private pool and
//     keeps only the returned address (video_asset_malloc / _write / _free);
//   * a frame  — each frame the game hands over self-contained primitives; the
//     console orders them by depth (the ordering table) and draws them at flush.
//
// A primitive references its uploaded data by address, exactly as a voice
// references its sample. There is no global drawing state: because the console
// re-orders primitives by depth, every attribute travels inside the primitive.
//
// The hardware geometry (screen size, texture limits, memory, frame budget) is
// IMPLEMENTATION-defined — the PDK carries no numbers. The game queries it (see
// the geometry section below) in rv_de::disc_initialize and validates its own
// baked assumptions against the answers; on a mismatch it returns a negative
// rv_err and the console refuses to run the disc. The reference console's
// defaults live in docs/platform/specs.md.
//
// Methods return >= 0 on success, a negative rv_err on failure.
//
// DEFERRED: the blending / texture-combine primitive flags, VRAM readback, and
// the display/output stage (rv_pixel).
class rv_cv {
   public:
    virtual ~rv_cv() = default;

    // --- hardware geometry: the game asks, the console answers ---

    // Native framebuffer size, in pixels. A frame is always the whole screen
    // (there is no drawing-area / scissor state). Stable for the whole session
    // — query once at disc_initialize.
    virtual int64_t screen_width() = 0;
    virtual int64_t screen_height() = 0;

    // Largest texture video_asset_write() accepts, in texels per axis.
    virtual int64_t texture_max_width() = 0;
    virtual int64_t texture_max_height() = 0;

    // Total video RAM the video_asset_* pool manages, in bytes.
    virtual int64_t video_memory_size() = 0;

    // How many primitives one frame accepts before frame_put() reports
    // RV_ERR_NOMEM.
    virtual int64_t frame_capacity() = 0;

    // --- video RAM: reserve, fill, release ---

    // Reserve `size` bytes of video RAM.
    //
    // Returns the address of the region (>= 0), or a negative rv_err:
    //   RV_ERR_INVAL  `size` is not positive
    //   RV_ERR_NOMEM  the pool is full
    //
    // The address is opaque: hand it to video_asset_write, video_asset_free and
    // rv_polygon::addr_texture / addr_palette. It is not a pointer.
    virtual int64_t video_asset_malloc(int64_t size) = 0;

    // Upload texture (or palette) data into the region at `addr` (from
    // video_asset_malloc). The block is self-describing (rv_texture::size), so no
    // separate length argument.
    //
    // Returns RV_OK, or a negative rv_err:
    //   RV_ERR_INVAL  unknown `addr`, the data does not fit, or the texture
    //                 exceeds texture_max_width() / texture_max_height()
    //
    // The bytes are copied during the call; the game may release its own buffer
    // once this returns.
    virtual int64_t video_asset_write(int64_t addr, rv_texture texture) = 0;

    // Release the region at `addr` (the value video_asset_malloc returned).
    //
    // Returns RV_OK, or a negative rv_err:
    //   RV_ERR_INVAL  unknown `addr`
    virtual int64_t video_asset_free(int64_t addr) = 0;

    // --- the frame ---

    // Set up the frame being built: `config` is a bitmask of
    // rv_pipeline_buffer_config_type, and `clear_color` is the background — the
    // console clears the framebuffer to it before drawing the frame's
    // primitives. Call once, before the frame's primitives; the ordering table
    // is always on, the flags only layer on top of it.
    //
    // Returns RV_OK, or a negative rv_err (RV_ERR_INVAL for an unknown flag).
    virtual int64_t frame_configure(uint64_t config, rv_color clear_color) = 0;

    // File one primitive into the current frame. It is not drawn now: it is stored
    // by rv_primitive::depth and rendered at frame_flush().
    //
    // Returns RV_OK, or a negative rv_err:
    //   RV_ERR_INVAL  unknown type, a polygon vertex_count other than 3 or 4,
    //                 or an address the primitive names is unknown
    //   RV_ERR_NOMEM  the frame's primitive buffer is full (frame_capacity())
    virtual int64_t frame_put(rv_primitive primitive) = 0;

    // Render every filed primitive in ascending depth order (far-to-near), then
    // present the frame and begin the next one.
    //
    // Returns RV_OK, or a negative rv_err.
    virtual int64_t frame_flush() = 0;
};

}  // namespace rv_3dmppc
