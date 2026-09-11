// Per-pixel core shared by rv_pcraster.cpp and rv_pcraster_poly.cpp; inline, so neither pays a call per pixel.
#pragma once

#include "pdklib/rv_textures/rv_texel_pack.hpp"
#include "rv_pconsole/cv/rv_pcraster.hpp"

namespace rv_3dmppc
{

namespace rv_pcraster_px
{

// THEOREM: ordered dithering, Bayer 4x4 — quantizing 8 bits to 5 throws away 3
// bits, and plain truncation turns a smooth gradient into 32 visible bands. The
// Bayer matrix is the recursively built threshold map whose entries are maximally
// spread apart, so instead of banding the error becomes a fixed, non-clumping
// 4x4 pattern the eye averages back into the original shade. Two properties
// matter here: the threshold depends only on (x & 3, y & 3), so it costs a table
// lookup and no state; and the 16 entries are the integers 0..15 exactly once,
// so the mean of `threshold >> 1` is 3.5 — precisely the average error truncating
// 3 bits introduces, which is why adding it before the shift keeps the average
// brightness correct instead of darkening the frame.
//
// The console is committed to "16-bit + dithering"; this is
// that dithering, and it is a large part of why the output reads as PSX-era.
inline constexpr int32_t RV_BAYER4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

inline int64_t min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}
inline int64_t max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

inline uint8_t clamp_channel(double value)
{
    if (value <= 0.0) {
        return 0;
    }
    if (value >= 255.0) {
        return 255;
    }
    return static_cast<uint8_t>(value + 0.5);
}

// One pixel, already known to be on-screen. This is the ONLY place the Z test
// lives, so every primitive path obeys it identically.
inline void emit(rv_pcfbuf &fbuf, int64_t x, int64_t y, uint16_t rgb555, int32_t depth, bool z_enabled)
{
    if (z_enabled) {
        if (!fbuf.depth_accept(x, y, depth)) {
            return;
        }
        fbuf.depth_store(x, y, depth);
    }
    fbuf.plot(x, y, rgb555);
}

// THEOREM: fixed point for uv (16.16) — a texture coordinate is affine in screen
// space exactly like an edge function, so it obeys the same recurrence
//   u(x + 1, y) = u(x, y) + du/dx,
// and the inner loop can be one integer add per axis instead of a per-pixel
// weighted sum. The gradient is a ratio of integers and is almost never one, so
// it needs a fraction: 16 fractional bits keep the drift over a full 4096-pixel
// span below 1/16 of a texel (the accumulated error is exact — the ONLY rounding
// is the single division that builds the gradient), while leaving 47 bits of
// integer headroom, which is more than any coordinate this console can reach.
// The alternative, recomputing u from the barycentric weights per pixel, would
// put a float multiply-add per axis in the hottest loop of the renderer for a
// precision nobody can see at 320x240.
inline constexpr int RV_UV_FX_SHIFT = 16;
inline constexpr int64_t RV_UV_FX_ONE = static_cast<int64_t>(1) << RV_UV_FX_SHIFT;

// Gradients are clamped to this magnitude (2^24 texels per pixel). A gradient
// that large only comes out of a sliver triangle whose doubled area is a handful
// of units — the texture on it is noise either way — and the clamp is what keeps
// the accumulator's arithmetic provably inside int64 for every input the
// contract allows (coordinates are int16, uv is uint16).
inline constexpr int64_t RV_UV_FX_LIMIT = static_cast<int64_t>(1) << 40;

inline int64_t fx_clamp(int64_t value)
{
    if (value > RV_UV_FX_LIMIT) {
        return RV_UV_FX_LIMIT;
    }
    if (value < -RV_UV_FX_LIMIT) {
        return -RV_UV_FX_LIMIT;
    }
    return value;
}

// numerator / denominator, in 16.16. The caller keeps |numerator| below 2^47 so
// the shift cannot overflow; every call site here is bounded by the int16
// coordinate and uint16 uv ranges.
inline int64_t fx_ratio(int64_t numerator, int64_t denominator)
{
    if (denominator == 0) {
        return 0;
    }
    return fx_clamp((numerator << RV_UV_FX_SHIFT) / denominator);
}

} // namespace rv_pcraster_px

using namespace rv_pcraster_px;

inline uint16_t rv_pcraster::pack_rgb555_dithered(rv_color color, int64_t x, int64_t y)
{
    // See the RV_BAYER4 theorem above. The threshold is halved because the
    // matrix spans 0..15 while the bits being dropped span 0..7.
    const size_t tile_y = static_cast<size_t>(y & 3);
    const size_t tile_x = static_cast<size_t>(x & 3);
    const int32_t threshold = RV_BAYER4[tile_y][tile_x] >> 1;

    int32_t r = static_cast<int32_t>(color.r) + threshold;
    int32_t g = static_cast<int32_t>(color.g) + threshold;
    int32_t b = static_cast<int32_t>(color.b) + threshold;
    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }

    // Truncation and not rounding: the threshold above was chosen against the
    // bits `>> 3` drops, so rounding on top of it would cancel half the dither.
    return rv_pdklib::rv_texel_pack(
        rv_pdklib::rv_texel_truncate(rv_color{ static_cast<uint8_t>(r), static_cast<uint8_t>(g),
            static_cast<uint8_t>(b) }));
}

// THEOREM: one quantizer for every pixel, samples included — a texel takes the
// same 24 -> 15 bit dither path as a flat fill, so there is a single place where
// colour becomes framebuffer, and nothing has to be kept in sync when the
// quantizer changes.
//
// The widening used here is c8 = c5 << 3, NOT the display's bit replication
// ((c5 << 3) | (c5 >> 2)). That choice is what makes this call the IDENTITY on
// an unmodulated texel: the Bayer threshold spans 0..7, the widened value has
// three zero low bits, so threshold + low bits never carries into bit 3 and the
// texel comes back out bit-for-bit — a raw texture is reproduced exactly, with
// no shimmer added to flat areas. Replication would leave up to 7 in the low
// bits and let the dither push texels a level up at random, which on a raw
// texture is pure noise: the texel was already an exact 5-bit value, so there is
// no quantization error to spread.
//
// It is not dead arithmetic, though. The moment texture-combine (modulation)
// lands, the value entering here is a texel MULTIPLIED by a vertex colour — a
// genuine 8-bit-per-channel quantity with a real error to dither — and this same
// call starts doing the work, without the sampling paths changing at all.
//
// Bit 15 (STP) is carried through untouched: it is texture data, not colour, and
// the blending modes that will read it are DEFERRED.
inline uint16_t rv_pcraster::dither_rgb555(uint16_t texel, int64_t x, int64_t y)
{
    rv_color color;
    color.r = static_cast<uint8_t>((texel & 0x1FU) << 3);
    color.g = static_cast<uint8_t>(((texel >> 5) & 0x1FU) << 3);
    color.b = static_cast<uint8_t>(((texel >> 10) & 0x1FU) << 3);

    const uint16_t stp = static_cast<uint16_t>(texel & 0x8000U);
    return static_cast<uint16_t>(pack_rgb555_dithered(color, x, y) | stp);
}

} // namespace rv_3dmppc
