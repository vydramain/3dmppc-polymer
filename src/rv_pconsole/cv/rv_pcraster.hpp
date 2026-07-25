// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 3 — растеризатор примитивов в кадровый буфер.
// ──────────────────────────────────────────────────────────────────────────────
//
// The software rasterizer. It turns one already-ordered primitive into pixels in
// an rv_pcfbuf and knows nothing else: no video RAM, no ordering table, no host.
// Everything it needs travels in the primitive (the contract's "no global drawing
// state" rule), plus the frame-wide depth key and Z flag the caller passes in.
//
// PATTERN: stateless service. Only static functions — the rasterizer owns no
// state at all, so a frame's primitives can be drawn in any order the ordering
// table dictates without a reset in between, and the screen bounds come from the
// framebuffer it is handed rather than from a configuration of its own.
//
// DEFERRED: RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE. It rasterizes as
// FLAT_COLOURED for now; rv_pccv notices the mode and warns once per frame.
#pragma once

#include <cstdint>

#include "pdk/cv/rv_primitives.hpp"
#include "pdk/cv/rv_vertex.hpp"
#include "rv_pconsole/cv/rv_pcfbuf.hpp"

namespace rv_3dmppc {

class rv_pcraster {
   public:
    // `depth` is the primitive's ordering key, used as the per-pixel Z value
    // when `z_enabled`; see rv_pccv::frame_flush for why a single key per
    // primitive is still worth testing per pixel.
    static void draw_line(rv_pcfbuf& fbuf, const rv_line& line, int32_t depth, bool z_enabled);
    static void draw_sprite(rv_pcfbuf& fbuf, const rv_sprite& sprite, int32_t depth,
                            bool z_enabled);
    static void draw_polygon(rv_pcfbuf& fbuf, const rv_polygon& polygon, int32_t depth,
                             bool z_enabled);

    // Draw whichever variant `primitive.type` selects. Unknown types are
    // ignored — frame_put already rejected them, this is only belt and braces.
    static void draw(rv_pcfbuf& fbuf, const rv_primitive& primitive, bool z_enabled);

    // True when the primitive asked for the DEFERRED texture path. rv_pccv uses
    // it to raise its once-per-frame warning; the rasterizer keeps no counter of
    // its own (see PATTERN: stateless service above).
    static bool uses_texture_sampling(const rv_primitive& primitive);

    // 8-bit-per-channel colour -> RGB555. `pack_rgb555` rounds; the dithered
    // form spreads the rounding error over a 4x4 pixel neighbourhood and is what
    // every interior pixel goes through.
    static uint16_t pack_rgb555(rv_color color);
    static uint16_t pack_rgb555_dithered(rv_color color, int64_t x, int64_t y);
};

}  // namespace rv_3dmppc
