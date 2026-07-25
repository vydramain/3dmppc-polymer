// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 4 — растеризатор: интерполяция uv и сэмплирование текстур.
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
// PATTERN: dependency inversion on video RAM. A textured primitive names its
// texels by ADDRESS, and this class still has no idea what an address is: the
// caller (rv_pccv) resolves it into an rv_pctexview and hands the view down.
// That boundary is deliberate — it keeps the rasterizer drivable from a test
// with a view over a stack array, and keeps the pool's lifetime rules in the one
// class that owns the pool.
#pragma once

#include <cstdint>

#include "pdk/cv/rv_primitives.hpp"
#include "pdk/cv/rv_vertex.hpp"
#include "rv_pconsole/cv/rv_pcfbuf.hpp"
#include "rv_pconsole/cv/rv_pctexel.hpp"

namespace rv_3dmppc {

class rv_pcraster {
   public:
    // `depth` is the primitive's ordering key, used as the per-pixel Z value
    // when `z_enabled`; see rv_pccv::frame_flush for why a single key per
    // primitive is still worth testing per pixel.
    //
    // `texture` is the view the caller resolved for THIS primitive. Passing an
    // invalid view (the default-constructed one) is always legal and means "no
    // texture": a SAMPLE_TEXTURE primitive whose region was never uploaded into
    // then degrades to a flat fill instead of vanishing, which is far easier to
    // recognize on screen than a missing polygon.
    //
    // draw_line takes no view on purpose: the contract (rv_primitives.hpp) says
    // a line is never textured and ignores its vertices' uv.
    static void draw_line(rv_pcfbuf& fbuf, const rv_line& line, int32_t depth, bool z_enabled);
    static void draw_sprite(rv_pcfbuf& fbuf, const rv_sprite& sprite, const rv_pctexview& texture,
                            int32_t depth, bool z_enabled);
    static void draw_polygon(rv_pcfbuf& fbuf, const rv_polygon& polygon,
                             const rv_pctexview& texture, int32_t depth, bool z_enabled);

    // Draw whichever variant `primitive.type` selects. Unknown types are
    // ignored — frame_put already rejected them, this is only belt and braces.
    static void draw(rv_pcfbuf& fbuf, const rv_primitive& primitive, const rv_pctexview& texture,
                     bool z_enabled);

    // 8-bit-per-channel colour -> RGB555. `pack_rgb555` rounds; the dithered
    // form spreads the rounding error over a 4x4 pixel neighbourhood and is what
    // every interior pixel goes through.
    static uint16_t pack_rgb555(rv_color color);
    static uint16_t pack_rgb555_dithered(rv_color color, int64_t x, int64_t y);

    // The same dither, applied to a texel that is already RGB555. Every pixel
    // this class writes goes through one of these three, so the quantizer lives
    // in exactly one place — see the theorem in rv_pcraster.cpp for why this one
    // is currently an identity and why it is still the right call to make.
    static uint16_t dither_rgb555(uint16_t texel, int64_t x, int64_t y);
};

}  // namespace rv_3dmppc
