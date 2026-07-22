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
// Methods return >= 0 on success, a negative rv_err on failure.
//
// The hardware numbers (1 MB video RAM pool, 4096 primitives per frame, 1024
// ordering-table buckets, 256x256 max texture) are SPEC values — see the README
// "Video" section. They may grow in a later console revision; the contract
// shapes do not change with them.
//
// DEFERRED: the blending / texture-combine primitive flags, VRAM readback, and
// the display/output stage (rv_pixel).
class rv_cv {
   public:
    virtual ~rv_cv() = default;

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
    //                 exceeds the spec size limit (256x256)
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
    //   RV_ERR_NOMEM  the frame's primitive buffer is full
    virtual int64_t frame_put(rv_primitive primitive) = 0;

    // Render every filed primitive in ascending depth order (far-to-near), then
    // present the frame and begin the next one.
    //
    // Returns RV_OK, or a negative rv_err.
    virtual int64_t frame_flush() = 0;
};

}  // namespace rv_3dmppc
