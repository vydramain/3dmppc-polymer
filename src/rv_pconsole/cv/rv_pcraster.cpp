// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 4 — растеризатор: uv в фиксированной точке и выборка текселей.
// ──────────────────────────────────────────────────────────────────────────────
#include "rv_pconsole/cv/rv_pcraster.hpp"

#include <cmath>

namespace rv_3dmppc {

namespace {

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
// docs/platform/specs.md commits the console to "16-bit + dithering"; this is
// that dithering, and it is a large part of why the output reads as PSX-era.
constexpr int32_t RV_BAYER4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

int64_t abs64(int64_t value) { return value < 0 ? -value : value; }

int64_t min64(int64_t a, int64_t b) { return a < b ? a : b; }
int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }

uint8_t clamp_channel(double value) {
    if (value <= 0.0) {
        return 0;
    }
    if (value >= 255.0) {
        return 255;
    }
    return static_cast<uint8_t>(value + 0.5);
}

rv_color lerp_color(const rv_color& a, const rv_color& b, double t) {
    rv_color out;
    out.r = clamp_channel(static_cast<double>(a.r) + (static_cast<double>(b.r) - a.r) * t);
    out.g = clamp_channel(static_cast<double>(a.g) + (static_cast<double>(b.g) - a.g) * t);
    out.b = clamp_channel(static_cast<double>(a.b) + (static_cast<double>(b.b) - a.b) * t);
    return out;
}

// One pixel, already known to be on-screen. This is the ONLY place the Z test
// lives, so every primitive path obeys it identically.
void emit(rv_pcfbuf& fbuf, int64_t x, int64_t y, uint16_t rgb555, int32_t depth, bool z_enabled) {
    if (z_enabled) {
        if (!fbuf.depth_accept(x, y, depth)) {
            return;
        }
        fbuf.depth_store(x, y, depth);
    }
    fbuf.plot(x, y, rgb555);
}

// THEOREM: edge function (half-plane test, Pineda 1988) —
//   E(p) = (p.x - x0) * (y1 - y0) - (p.y - y0) * (x1 - x0)
// is the 2D cross product of the edge vector with the vector to p, i.e. twice the
// signed area of the triangle (v0, v1, p). Its SIGN says which side of the
// infinite line through v0->v1 the point lies on, so "inside the triangle" is
// three sign tests and nothing else — no slopes, no division, no special case for
// horizontal edges or for which vertex is topmost.
//
// The property the inner loop is built on: E is AFFINE in p. Therefore
//   E(x + 1, y) = E(x, y) + (y1 - y0)   and   E(x, y + 1) = E(x, y) - (x1 - x0),
// so after one setup per triangle each pixel step costs a single integer add per
// edge. Evaluated in int64 because screen coordinates are int16 and the products
// above reach ~2^32, which would silently overflow int32 for an off-screen vertex.
int64_t edge_at(int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t px, int64_t py) {
    return (px - x0) * (y1 - y0) - (py - y0) * (x1 - x0);
}

// THEOREM: top-left fill rule — a pixel exactly ON a shared edge satisfies E == 0
// for BOTH triangles that meet there. Accepting it in both draws it twice (a
// visible seam once blending lands, and a wasted write now); rejecting it in both
// leaves a one-pixel crack. The fix is a tie-break that depends only on the edge's
// DIRECTION: accept E == 0 only on edges that are "top" (horizontal with the
// interior below) or "left" (the interior to their right). With the winding
// normalized (positive doubled area) the interior lies where E > 0, so the
// interior direction is the gradient (dy, -dx): "top" is dy == 0 && dx < 0, and
// "left" is dy > 0.
//
// The guarantee: a shared edge is traversed in OPPOSITE directions by the two
// triangles, and is_top_left(d) != is_top_left(-d) for every non-degenerate d, so
// exactly one of the two claims the boundary pixel. Critical for quads, whose two
// halves share the (2,3) diagonal by construction.
bool is_top_left(int64_t dx, int64_t dy) { return dy > 0 || (dy == 0 && dx < 0); }

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
constexpr int RV_UV_FX_SHIFT = 16;
constexpr int64_t RV_UV_FX_ONE = static_cast<int64_t>(1) << RV_UV_FX_SHIFT;

// Gradients are clamped to this magnitude (2^24 texels per pixel). A gradient
// that large only comes out of a sliver triangle whose doubled area is a handful
// of units — the texture on it is noise either way — and the clamp is what keeps
// the accumulator's arithmetic provably inside int64 for every input the
// contract allows (coordinates are int16, uv is uint16).
constexpr int64_t RV_UV_FX_LIMIT = static_cast<int64_t>(1) << 40;

int64_t fx_clamp(int64_t value) {
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
int64_t fx_ratio(int64_t numerator, int64_t denominator) {
    if (denominator == 0) {
        return 0;
    }
    return fx_clamp((numerator << RV_UV_FX_SHIFT) / denominator);
}

// Where a primitive's texels come from, resolved by the caller. A null (or
// invalid) `view` means "fill with colour" and is the whole of the untextured
// path — no second code path, no flag to keep in sync.
//
// The box is the primitive's own bounding box in screen space, UNCLIPPED: it is
// what RV_TEXWRAP_STRETCH stretches the texture across, and taking the clipped
// box instead would rescale the texture whenever the primitive touched a screen
// edge.
struct rv_pctexstage {
    const rv_pctexview* view = nullptr;
    rv_texture_mapping_type mapping = RV_TEXWRAP_CLAMP;

    int64_t box_x = 0;
    int64_t box_y = 0;
    int64_t box_w = 0;
    int64_t box_h = 0;

    bool active() const { return view != nullptr && view->valid(); }
    bool stretch() const { return mapping == RV_TEXWRAP_STRETCH; }
};

// A 16.16 texture coordinate and its two screen-space gradients. `u`/`v` hold
// the value at the start of the current row; the row loop steps them by the d/dy
// pair, the pixel loop by the d/dx pair.
struct rv_pcuvwalk {
    int64_t u = 0;
    int64_t v = 0;
    int64_t du_dx = 0;
    int64_t dv_dx = 0;
    int64_t du_dy = 0;
    int64_t dv_dy = 0;
};

struct rv_pctri {
    int64_t x[3];
    int64_t y[3];
    rv_color color[3];
    rv_uv uv[3];
};

void fill_triangle(rv_pcfbuf& fbuf, rv_pctri tri, const rv_pctexstage& stage, int32_t depth,
                   bool z_enabled) {
    // THEOREM: signed area — area2 = E(v0, v1, v2) is twice the signed area of
    // the triangle. Its sign is the winding; a negative area means every edge
    // function has the opposite sense and the "all E >= 0" test would reject the
    // whole interior. Swapping any two vertices flips the winding, so one
    // conditional swap normalizes every input to the positive case and the inner
    // loop needs no orientation branch.
    //
    // Note what this deliberately does NOT do: back-face culling. The winding is
    // normalized, never rejected — the console draws whatever the disc hands it
    // (the contract names no facing rule, and a disc that wants culling does it
    // in its own transform stage where it still has a normal to test).
    int64_t area2 = edge_at(tri.x[0], tri.y[0], tri.x[1], tri.y[1], tri.x[2], tri.y[2]);
    if (area2 == 0) {
        return;  // degenerate: zero-area triangles cover no pixel centre
    }
    if (area2 < 0) {
        const int64_t sx = tri.x[1];
        const int64_t sy = tri.y[1];
        const rv_color sc = tri.color[1];
        const rv_uv suv = tri.uv[1];
        tri.x[1] = tri.x[2];
        tri.y[1] = tri.y[2];
        tri.color[1] = tri.color[2];
        tri.uv[1] = tri.uv[2];
        tri.x[2] = sx;
        tri.y[2] = sy;
        tri.color[2] = sc;
        tri.uv[2] = suv;
        area2 = -area2;
    }

    // THEOREM: bounding box ∩ screen — the triangle covers no pixel outside the
    // box spanned by its vertices, and the console has no scissor state beyond
    // the screen itself (rv_cv: "a frame is always the whole screen"). So the
    // intersection of the two rectangles is the complete clip: no polygon
    // clipping against the frustum, no per-pixel bounds test in the inner loop,
    // and an entirely off-screen primitive costs only this comparison.
    int64_t min_x = max64(min64(tri.x[0], min64(tri.x[1], tri.x[2])), 0);
    int64_t min_y = max64(min64(tri.y[0], min64(tri.y[1], tri.y[2])), 0);
    int64_t max_x = min64(max64(tri.x[0], max64(tri.x[1], tri.x[2])), fbuf.width() - 1);
    int64_t max_y = min64(max64(tri.y[0], max64(tri.y[1], tri.y[2])), fbuf.height() - 1);
    if (min_x > max_x || min_y > max_y) {
        return;
    }

    // Edge i runs from vertex i to vertex (i + 1) % 3; the vertex it does NOT
    // touch is (i + 2) % 3.
    int64_t step_x[3];
    int64_t step_y[3];
    int64_t bias[3];
    int64_t row[3];
    for (int i = 0; i < 3; ++i) {
        const int a = i;
        const int b = (i + 1) % 3;
        const int64_t dx = tri.x[b] - tri.x[a];
        const int64_t dy = tri.y[b] - tri.y[a];

        step_x[i] = dy;   // dE/dx
        step_y[i] = -dx;  // dE/dy
        bias[i] = is_top_left(dx, dy) ? 0 : -1;
        row[i] = edge_at(tri.x[a], tri.y[a], tri.x[b], tri.y[b], min_x, min_y);
    }

    // THEOREM: barycentric coordinates from the same edge functions — for a
    // point p inside, E_i(p) is twice the area of the sub-triangle opposite
    // vertex (i + 2) % 3, so w_(i+2) = E_i(p) / area2. The three weights are
    // non-negative, sum to 1 (the three sub-areas tile the triangle), and equal
    // (1,0,0) at v0 and so on — exactly the affine interpolation Gouraud
    // shading needs. Reusing the numbers the inside test already computed means
    // vertex-colour interpolation costs no extra geometry work and, since each
    // E is stepped incrementally, NO division per pixel: only the reciprocal of
    // area2, hoisted out of both loops.
    const double inv_area2 = 1.0 / static_cast<double>(area2);

    const bool textured = stage.active();

    // THEOREM: AFFINE texture mapping — uv is interpolated with the very same
    // screen-space barycentrics as the vertex colours, NOT divided through by a
    // per-vertex 1/w. This is not a shortcut a later stage repairs: an rv_vertex
    // carries x, y, colour and uv and no w at all (pdk/cv/rv_vertex.hpp — the
    // disc hands the console screen positions, not clip-space points), so the
    // information perspective correction needs does not exist on this side of the
    // contract and cannot be reconstructed here. It is inherited from the PSX on
    // purpose, not missed.
    //
    // The visible consequence, and the reason the contract is shaped this way: a
    // polygon receding into the screen has its texture interpolated linearly in
    // SCREEN space rather than along the surface, so the texels swim as it turns
    // and the two halves of a quad visibly disagree along their shared diagonal.
    // Worst on large, steeply angled surfaces (floors, walls); invisible on small
    // or screen-parallel ones. A disc manages it exactly as PSX games did — by
    // subdividing a big surface into more, smaller polygons.
    rv_pcuvwalk uv;
    if (textured) {
        if (stage.stretch()) {
            // STRETCH ignores the vertex uv entirely: the texture is mapped onto
            // the primitive's bounding box, so the gradient is one texture per
            // box and there are no cross terms.
            uv.du_dx = fx_ratio(stage.view->width, max64(stage.box_w, 1));
            uv.dv_dy = fx_ratio(stage.view->height, max64(stage.box_h, 1));
            uv.u = (min_x - stage.box_x) * uv.du_dx;
            uv.v = (min_y - stage.box_y) * uv.dv_dy;
        } else {
            const int64_t u0 = tri.uv[0].u;
            const int64_t u1 = tri.uv[1].u;
            const int64_t u2 = tri.uv[2].u;
            const int64_t v0 = tri.uv[0].v;
            const int64_t v1 = tri.uv[1].v;
            const int64_t v2 = tri.uv[2].v;

            // u(p) = (E1 * u0 + E2 * u1 + E0 * u2) / area2 — the weights above —
            // and every E is affine with the steps already computed, so the two
            // gradients cost one division each for the whole triangle.
            uv.du_dx = fx_ratio(step_x[1] * u0 + step_x[2] * u1 + step_x[0] * u2, area2);
            uv.du_dy = fx_ratio(step_y[1] * u0 + step_y[2] * u1 + step_y[0] * u2, area2);
            uv.dv_dx = fx_ratio(step_x[1] * v0 + step_x[2] * v1 + step_x[0] * v2, area2);
            uv.dv_dy = fx_ratio(step_y[1] * v0 + step_y[2] * v1 + step_y[0] * v2, area2);

            // Anchor at vertex 0, where the weights are exactly (1, 0, 0) and the
            // coordinate is exactly that vertex's uv — no division and no
            // rounding, so the texel the disc authored to sit on a corner is the
            // one texel guaranteed to land on it.
            uv.u = (u0 << RV_UV_FX_SHIFT) + (min_x - tri.x[0]) * uv.du_dx +
                   (min_y - tri.y[0]) * uv.du_dy;
            uv.v = (v0 << RV_UV_FX_SHIFT) + (min_x - tri.x[0]) * uv.dv_dx +
                   (min_y - tri.y[0]) * uv.dv_dy;
        }
    }

    for (int64_t y = min_y; y <= max_y; ++y) {
        int64_t e0 = row[0];
        int64_t e1 = row[1];
        int64_t e2 = row[2];
        int64_t u_fx = uv.u;
        int64_t v_fx = uv.v;

        for (int64_t x = min_x; x <= max_x; ++x) {
            if ((e0 + bias[0]) >= 0 && (e1 + bias[1]) >= 0 && (e2 + bias[2]) >= 0) {
                if (textured) {
                    // >> on a signed value floors (C++20 onwards), so a
                    // coordinate lands in the same texel on both sides of zero —
                    // no half-texel jump across u == 0 under TILE.
                    const rv_pctexel_sample texel = rv_pctexel::sample(
                        *stage.view, u_fx >> RV_UV_FX_SHIFT, v_fx >> RV_UV_FX_SHIFT, stage.mapping);
                    // A transparent texel writes NOTHING — not colour, not
                    // depth. The Z test is inside emit(), so simply not calling
                    // it is the whole rule (see rv_pctexel.cpp).
                    if (texel.drawn) {
                        emit(fbuf, x, y, rv_pcraster::dither_rgb555(texel.value, x, y), depth,
                             z_enabled);
                    }
                } else {
                    const double w0 = static_cast<double>(e1) * inv_area2;  // opposite edge 1
                    const double w1 = static_cast<double>(e2) * inv_area2;  // opposite edge 2
                    const double w2 = static_cast<double>(e0) * inv_area2;  // opposite edge 0

                    rv_color color;
                    color.r = clamp_channel(w0 * tri.color[0].r + w1 * tri.color[1].r +
                                            w2 * tri.color[2].r);
                    color.g = clamp_channel(w0 * tri.color[0].g + w1 * tri.color[1].g +
                                            w2 * tri.color[2].g);
                    color.b = clamp_channel(w0 * tri.color[0].b + w1 * tri.color[1].b +
                                            w2 * tri.color[2].b);

                    emit(fbuf, x, y, rv_pcraster::pack_rgb555_dithered(color, x, y), depth,
                         z_enabled);
                }
            }

            e0 += step_x[0];
            e1 += step_x[1];
            e2 += step_x[2];
            u_fx += uv.du_dx;
            v_fx += uv.dv_dx;
        }

        row[0] += step_y[0];
        row[1] += step_y[1];
        row[2] += step_y[2];
        uv.u += uv.du_dy;
        uv.v += uv.dv_dy;
    }
}

rv_line make_edge(const rv_vertex& a, const rv_vertex& b) {
    rv_line line;
    line.vertexes[0] = a;
    line.vertexes[1] = b;
    return line;
}

}  // namespace

uint16_t rv_pcraster::pack_rgb555(rv_color color) {
    // Round-to-nearest of c8 * 31 / 255. Used for flat, position-independent
    // conversions such as the frame clear colour, where a dither pattern would
    // just be noise on an untextured background.
    const uint32_t r5 = (static_cast<uint32_t>(color.r) * 31U + 127U) / 255U;
    const uint32_t g5 = (static_cast<uint32_t>(color.g) * 31U + 127U) / 255U;
    const uint32_t b5 = (static_cast<uint32_t>(color.b) * 31U + 127U) / 255U;
    return static_cast<uint16_t>(r5 | (g5 << 5) | (b5 << 10));
}

uint16_t rv_pcraster::pack_rgb555_dithered(rv_color color, int64_t x, int64_t y) {
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

    const uint32_t r5 = static_cast<uint32_t>(r >> 3);
    const uint32_t g5 = static_cast<uint32_t>(g >> 3);
    const uint32_t b5 = static_cast<uint32_t>(b >> 3);
    return static_cast<uint16_t>(r5 | (g5 << 5) | (b5 << 10));
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
uint16_t rv_pcraster::dither_rgb555(uint16_t texel, int64_t x, int64_t y) {
    rv_color color;
    color.r = static_cast<uint8_t>((texel & 0x1FU) << 3);
    color.g = static_cast<uint8_t>(((texel >> 5) & 0x1FU) << 3);
    color.b = static_cast<uint8_t>(((texel >> 10) & 0x1FU) << 3);

    const uint16_t stp = static_cast<uint16_t>(texel & 0x8000U);
    return static_cast<uint16_t>(pack_rgb555_dithered(color, x, y) | stp);
}

void rv_pcraster::draw_line(rv_pcfbuf& fbuf, const rv_line& line, int32_t depth, bool z_enabled) {
    const rv_vertex& a = line.vertexes[0];
    const rv_vertex& b = line.vertexes[1];

    const int64_t x0 = a.x;
    const int64_t y0 = a.y;
    const int64_t dx = static_cast<int64_t>(b.x) - x0;
    const int64_t dy = static_cast<int64_t>(b.y) - y0;

    // THEOREM: DDA — parameterize the segment as p(t) = p0 + t * (p1 - p0) and
    // step t in max(|dx|, |dy|) equal increments. Choosing the longer axis as
    // the driving one is what makes the trace gapless: the fast axis advances by
    // at most one pixel per step, so consecutive samples are always neighbours.
    // Bresenham would avoid the floating point, but t is needed anyway to
    // interpolate the two vertex colours along the segment, so the parametric
    // form is the cheaper of the two here.
    const int64_t steps = max64(abs64(dx), abs64(dy));
    if (steps == 0) {
        if (fbuf.inside(x0, y0)) {
            emit(fbuf, x0, y0, pack_rgb555_dithered(a.color, x0, y0), depth, z_enabled);
        }
        return;
    }

    const double inv_steps = 1.0 / static_cast<double>(steps);
    for (int64_t i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) * inv_steps;
        const int64_t x = x0 + static_cast<int64_t>(std::llround(static_cast<double>(dx) * t));
        const int64_t y = y0 + static_cast<int64_t>(std::llround(static_cast<double>(dy) * t));

        // A line is a curve, not a box: clipping the endpoints would move them
        // and shear the interpolation, so the trace is clipped per sample.
        if (!fbuf.inside(x, y)) {
            continue;
        }
        const rv_color color = lerp_color(a.color, b.color, t);
        emit(fbuf, x, y, pack_rgb555_dithered(color, x, y), depth, z_enabled);
    }
}

void rv_pcraster::draw_sprite(rv_pcfbuf& fbuf, const rv_sprite& sprite, const rv_pctexview& texture,
                              int32_t depth, bool z_enabled) {
    if (sprite.width == 0 || sprite.height == 0) {
        return;
    }

    const int64_t x0 = sprite.x;
    const int64_t y0 = sprite.y;
    const int64_t x1 = x0 + static_cast<int64_t>(sprite.width);   // exclusive
    const int64_t y1 = y0 + static_cast<int64_t>(sprite.height);  // exclusive

    // Bounding box ∩ screen again — a sprite is axis-aligned by construction, so
    // the box IS the primitive and the clip is exact rather than conservative.
    const int64_t cx0 = max64(x0, 0);
    const int64_t cy0 = max64(y0, 0);
    const int64_t cx1 = min64(x1, fbuf.width());
    const int64_t cy1 = min64(y1, fbuf.height());
    if (cx0 >= cx1 || cy0 >= cy1) {
        return;
    }

    // A sprite that asked to sample but whose view came back invalid (a region
    // that was allocated and never uploaded into) falls back to the flat fill
    // below: a solid rectangle in the wrong colour is a far better bug report
    // than a primitive that silently disappears.
    const bool textured =
        sprite.fill_mode == RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE && texture.valid();

    if (textured) {
        // The texture is laid down from the sprite's UPPER-LEFT corner, one
        // texel per pixel — the contract's rule (rv_primitives.hpp), and the
        // reason a sprite carries no uv at all. CLAMP smears the last texel over
        // the overhang, TILE repeats; both are the sampler's job.
        //
        // STRETCH rescales instead: the whole texture is fitted onto the
        // sprite's own (unclipped) extent, so a clipped sprite keeps its scale.
        int64_t du_dx = RV_UV_FX_ONE;
        int64_t dv_dy = RV_UV_FX_ONE;
        if (sprite.mapping == RV_TEXWRAP_STRETCH) {
            du_dx = fx_ratio(texture.width, static_cast<int64_t>(sprite.width));
            dv_dy = fx_ratio(texture.height, static_cast<int64_t>(sprite.height));
        }

        int64_t v_row = (cy0 - y0) * dv_dy;
        const int64_t u_start = (cx0 - x0) * du_dx;

        for (int64_t y = cy0; y < cy1; ++y) {
            int64_t u_fx = u_start;
            for (int64_t x = cx0; x < cx1; ++x) {
                const rv_pctexel_sample texel = rv_pctexel::sample(
                    texture, u_fx >> RV_UV_FX_SHIFT, v_row >> RV_UV_FX_SHIFT, sprite.mapping);
                if (texel.drawn) {
                    emit(fbuf, x, y, dither_rgb555(texel.value, x, y), depth, z_enabled);
                }
                u_fx += du_dx;
            }
            v_row += dv_dy;
        }
        return;
    }

    const bool wireframe = sprite.fill_mode == RV_PRIMITIVE_FILL_MODE_WIREFRAME;

    for (int64_t y = cy0; y < cy1; ++y) {
        if (wireframe && y != y0 && y != y1 - 1) {
            // Interior row: only the two vertical sides, and only if the
            // unclipped side actually survived the clip.
            if (x0 >= cx0 && x0 < cx1) {
                emit(fbuf, x0, y, pack_rgb555_dithered(sprite.color, x0, y), depth, z_enabled);
            }
            if (x1 - 1 >= cx0 && x1 - 1 < cx1) {
                emit(fbuf, x1 - 1, y, pack_rgb555_dithered(sprite.color, x1 - 1, y), depth,
                     z_enabled);
            }
            continue;
        }

        for (int64_t x = cx0; x < cx1; ++x) {
            emit(fbuf, x, y, pack_rgb555_dithered(sprite.color, x, y), depth, z_enabled);
        }
    }
}

void rv_pcraster::draw_polygon(rv_pcfbuf& fbuf, const rv_polygon& polygon,
                               const rv_pctexview& texture, int32_t depth, bool z_enabled) {
    if (polygon.vertex_count != 3 && polygon.vertex_count != 4) {
        return;  // frame_put already rejected this; nothing sane to draw
    }
    const bool quad = polygon.vertex_count == 4;

    if (polygon.fill_mode == RV_PRIMITIVE_FILL_MODE_WIREFRAME) {
        // The PERIMETER only. For a quad the (2,3) diagonal is an interior edge
        // of the triangulation, and the contract says wireframe draws "only the
        // edges, not the interior" — so the outline follows the PSX quad vertex
        // order 1-2-4-3 rather than the two triangles' edge sets.
        if (quad) {
            draw_line(fbuf, make_edge(polygon.vertexes[0], polygon.vertexes[1]), depth, z_enabled);
            draw_line(fbuf, make_edge(polygon.vertexes[1], polygon.vertexes[3]), depth, z_enabled);
            draw_line(fbuf, make_edge(polygon.vertexes[3], polygon.vertexes[2]), depth, z_enabled);
            draw_line(fbuf, make_edge(polygon.vertexes[2], polygon.vertexes[0]), depth, z_enabled);
        } else {
            draw_line(fbuf, make_edge(polygon.vertexes[0], polygon.vertexes[1]), depth, z_enabled);
            draw_line(fbuf, make_edge(polygon.vertexes[1], polygon.vertexes[2]), depth, z_enabled);
            draw_line(fbuf, make_edge(polygon.vertexes[2], polygon.vertexes[0]), depth, z_enabled);
        }
        return;
    }

    // FLAT_COLOURED interpolates the vertex colours (equal colours = flat,
    // different = gouraud — the contract needs no separate shading flag);
    // SAMPLE_TEXTURE replaces that with a texel fetch per pixel. An invalid view
    // means the disc named a region it never uploaded into, and the polygon
    // falls back to its vertex colours rather than disappearing.
    //
    // A quad is drawn as the triangles (1,2,3) and (2,3,4) — 0-based (0,1,2) and
    // (1,2,3). The split is contract, not an implementation choice: it decides
    // how colours and uv interpolate across the surface, so vertex ORDER is part
    // of what the disc specifies (PSX rule, kept on purpose).
    static const int RV_TRI_INDICES[2][3] = {{0, 1, 2}, {1, 2, 3}};
    const int tri_count = quad ? 2 : 1;

    rv_pctexstage stage;
    if (polygon.fill_mode == RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE) {
        stage.view = &texture;
        stage.mapping = polygon.mapping;

        // The STRETCH box is the WHOLE polygon's screen bounding box, computed
        // once here and shared by both triangles of a quad. Letting each
        // triangle stretch over its own box would map the texture twice — once
        // per half — and split the quad down its diagonal.
        int64_t min_x = polygon.vertexes[0].x;
        int64_t min_y = polygon.vertexes[0].y;
        int64_t max_x = min_x;
        int64_t max_y = min_y;
        for (uint32_t i = 1; i < polygon.vertex_count; ++i) {
            min_x = min64(min_x, polygon.vertexes[i].x);
            min_y = min64(min_y, polygon.vertexes[i].y);
            max_x = max64(max_x, polygon.vertexes[i].x);
            max_y = max64(max_y, polygon.vertexes[i].y);
        }
        stage.box_x = min_x;
        stage.box_y = min_y;
        stage.box_w = max_x - min_x + 1;
        stage.box_h = max_y - min_y + 1;
    }

    for (int t = 0; t < tri_count; ++t) {
        rv_pctri tri;
        for (int i = 0; i < 3; ++i) {
            const rv_vertex& vertex = polygon.vertexes[RV_TRI_INDICES[t][i]];
            tri.x[i] = vertex.x;
            tri.y[i] = vertex.y;
            tri.color[i] = vertex.color;
            tri.uv[i] = vertex.uv;
        }
        fill_triangle(fbuf, tri, stage, depth, z_enabled);
    }
}

void rv_pcraster::draw(rv_pcfbuf& fbuf, const rv_primitive& primitive, const rv_pctexview& texture,
                       bool z_enabled) {
    // PATTERN: dispatch on the variant tag. rv_primitive is a tagged union (the
    // PDK is a C-shaped ABI, so no std::variant), and this is the one place that
    // reads the tag.
    switch (primitive.type) {
        case RV_PRIMITIVE_LINE:
            draw_line(fbuf, primitive.data.line, primitive.depth, z_enabled);
            break;
        case RV_PRIMITIVE_POLYGON:
            draw_polygon(fbuf, primitive.data.polygon, texture, primitive.depth, z_enabled);
            break;
        case RV_PRIMITIVE_SPRITE:
            draw_sprite(fbuf, primitive.data.sprite, texture, primitive.depth, z_enabled);
            break;
        default:
            break;
    }
}

}  // namespace rv_3dmppc
