// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 3 — кадровый буфер RGB555 с буфером глубины.
// ──────────────────────────────────────────────────────────────────────────────
//
// The console's framebuffer: one screen-sized page of 16-bit colour plus one
// screen-sized page of depth. Header-only because every method is a handful of
// array operations that the rasterizer calls per pixel — a translation-unit
// boundary here would be the single most expensive thing in the renderer.
//
// PATTERN: information hiding. The RGB555 packing (and the RGB555 -> ARGB8888
// expansion the host needs) exists in exactly ONE place, this class. The
// rasterizer speaks packed uint16_t, the host speaks 0xAARRGGBB, and neither
// knows the other's format.
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace rv_3dmppc {

class rv_pcfbuf {
   public:
    rv_pcfbuf(int64_t width, int64_t height)
        : width_(width > 0 ? width : 0),
          height_(height > 0 ? height : 0),
          color_(static_cast<size_t>(width_ * height_), 0),
          depth_(static_cast<size_t>(width_ * height_), std::numeric_limits<int32_t>::min()),
          argb_(static_cast<size_t>(width_ * height_), 0) {}

    int64_t width() const { return width_; }
    int64_t height() const { return height_; }

    // Start a new frame. `rgb555` is the background; the depth page is only
    // touched when the frame asked for per-pixel rejection, because clearing it
    // is a full screen-sized write the Z-less path has no use for.
    void clear(uint16_t rgb555, int32_t depth_value, bool clear_depth) {
        for (size_t i = 0; i < color_.size(); ++i) {
            color_[i] = rgb555;
        }
        if (clear_depth) {
            for (size_t i = 0; i < depth_.size(); ++i) {
                depth_[i] = depth_value;
            }
        }
    }

    // UNCHECKED write: the caller has already clipped to the screen. Every
    // rasterizer path here intersects its bounding box with the screen before
    // it starts stepping, so a bounds test per pixel would be pure waste.
    void plot(int64_t x, int64_t y, uint16_t rgb555) { color_[index(x, y)] = rgb555; }

    // Checked variant for the paths that walk a parametric curve instead of a
    // box — the DDA line, whose endpoints may sit far off-screen.
    void plot_checked(int64_t x, int64_t y, uint16_t rgb555) {
        if (inside(x, y)) {
            color_[index(x, y)] = rgb555;
        }
    }

    // The Z test. The contract's depth axis says LARGER = NEARER, so a pixel
    // survives only when nothing nearer has already been written there. Equal
    // depths are accepted on purpose: same-depth primitives must keep their
    // submission order (later put = drawn on top), and rejecting ties would
    // invert it.
    bool depth_accept(int64_t x, int64_t y, int32_t depth) const {
        return depth >= depth_[index(x, y)];
    }

    void depth_store(int64_t x, int64_t y, int32_t depth) { depth_[index(x, y)] = depth; }

    bool inside(int64_t x, int64_t y) const {
        return x >= 0 && y >= 0 && x < width_ && y < height_;
    }

    // Hand the finished page to the host as 0xAARRGGBB.
    //
    // THEOREM: bit replication — the correct 5 -> 8 bit widening is
    // c8 = round(c5 * 255 / 31), and (c5 << 3) | (c5 >> 2) equals it for every
    // one of the 32 inputs. Reason: c5/31 written in binary is the infinitely
    // repeating group c5c5c5..., so copying the top bits of c5 down into the
    // low bits is literally truncating that expansion. A plain c5 << 3 would
    // instead cap white at 248 and tint the whole picture dark.
    const uint32_t* expand_argb() {
        for (size_t i = 0; i < color_.size(); ++i) {
            const uint32_t texel = color_[i];
            const uint32_t r5 = texel & 0x1FU;
            const uint32_t g5 = (texel >> 5) & 0x1FU;
            const uint32_t b5 = (texel >> 10) & 0x1FU;

            const uint32_t r8 = (r5 << 3) | (r5 >> 2);
            const uint32_t g8 = (g5 << 3) | (g5 >> 2);
            const uint32_t b8 = (b5 << 3) | (b5 >> 2);

            // Bit 15 (STP) is stored but has no meaning yet — the blending
            // modes that read it are DEFERRED, so the frame is always opaque.
            argb_[i] = 0xFF000000U | (r8 << 16) | (g8 << 8) | b8;
        }
        return argb_.data();
    }

   private:
    size_t index(int64_t x, int64_t y) const { return static_cast<size_t>(y * width_ + x); }

    int64_t width_;
    int64_t height_;

    std::vector<uint16_t> color_;  // RGB555: 0-4 R, 5-9 G, 10-14 B, 15 STP
    std::vector<int32_t> depth_;
    std::vector<uint32_t> argb_;  // scratch page handed to the host at present
};

}  // namespace rv_3dmppc
