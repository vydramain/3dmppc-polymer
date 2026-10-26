// CPU z-buffer rasteriser. Coverage mirrors src/rv_pconsole/cv/rv_pcraster_poly.cpp in int64: edge_at,
// is_top_left and clip_bounds, and normalize_winding inline in rv_editor_zbuf_draw. Samples are pixel
// centres, not the console's corners (decision D4): the corners are doubled, so a centre is an integer.
// uv mirrors texel_uvs (src/rv_pconsole/cg/rv_pccg_sw_put.cpp) and setup_uv (src/rv_pconsole/cv/rv_pcraster_poly.cpp):
// integer texels, affine in screen space. Colour is the console's Gouraud rule in integers: screen-linear weights,
// each channel rounded half up. The console's shade_gouraud uses double weights, so the editor may differ by 1 LSB.
// Texel alpha 0 is the console's transparent texel 0000h (pdk/include/pdk/cv/rv_texel.h); TILE wraps the texel
// integer as rv_pctexel::wrap does.

#include "panes/rv_editor_zbuf.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <utility>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Colour values are 0..255; a product of two is divided by this, rounding half up.
constexpr int64_t RV_EDITOR_ZBUF_BYTE_MAX = 255;

// Interpolation keeps the top 40 bits of the edge values: an area2 wider than that is shifted down first, so the
// weighted sum (at most 255 times the area) stays in int64. With the shift the weighted mean is off by under 2e-9 LSB,
// so a value within that of a half-LSB step may round either way.
constexpr int RV_EDITOR_ZBUF_MIX_BITS = 40;

// Flat colour: an opaque white texel, so the vertex colour is the pixel colour.
constexpr rv_editor_zbuf_rgba RV_EDITOR_ZBUF_FLAT_TEXEL = { 255, 255, 255, 255 };

// Cleared depth: the farthest key, so the first triangle to touch a pixel always passes. Mirrors
// RV_PCCV_DEPTH_FARTHEST in src/rv_pconsole/cv/rv_pccv_sw.cpp.
constexpr int32_t RV_EDITOR_ZBUF_DEPTH_FARTHEST = std::numeric_limits<int32_t>::min();

// Largest texel coordinate of a corner, as the console's RV_PCCG_SW_TEXEL_MAX (src/rv_pconsole/cg/rv_pccg_sw_put.cpp).
// Above it the triangle is refused. It keeps the texel gradient's numerator below 2^47 (texel_gradient).
constexpr int64_t RV_EDITOR_ZBUF_TEXEL_MAX = 65535;

// Texel coordinates per pixel are 16.16, as RV_UV_FX_SHIFT in src/rv_pconsole/cv/rv_pcraster_pixel.hpp.
constexpr int RV_EDITOR_ZBUF_TEXEL_FRAC = 16;
constexpr int64_t RV_EDITOR_ZBUF_TEXEL_ONE = int64_t{ 1 } << RV_EDITOR_ZBUF_TEXEL_FRAC;

// Gradient clamp, texels per pixel in 16.16. The console clamps at 2^40 (RV_UV_FX_LIMIT), safe for int16 corners.
// Mirroring that at corners up to 2^28 needs the per-pixel product in __int128; this clamp is 2^30 instead, so the
// editor differs from the console only for slivers whose gradient exceeds 2^30. The owner decides whether to widen it.
constexpr int64_t RV_EDITOR_ZBUF_GRADIENT_LIMIT = int64_t{ 1 } << 30;

// A pixel centre is half a step past its corner: each gradient is halved by this floor shift.
constexpr int RV_EDITOR_ZBUF_HALF_STEP_SHIFT = 1;

// A point in doubled pixel units (RV_EDITOR_ZBUF_DOUBLING).
struct rv_zbuf_point {
    int64_t x = 0;
    int64_t y = 0;
};

// A textured triangle's texel coordinates per corner, and the 16.16 steps per pixel (affine in screen space).
struct rv_zbuf_texel {
    int64_t u[RV_EDITOR_ZBUF_CORNERS] = {};
    int64_t v[RV_EDITOR_ZBUF_CORNERS] = {};
    int64_t du_dx = 0;
    int64_t dv_dx = 0;
    int64_t du_dy = 0;
    int64_t dv_dy = 0;
};

// A triangle put in counter-clockwise order (area2 > 0), with its doubled points.
struct rv_zbuf_tri {
    rv_editor_zbuf_vertex corner[RV_EDITOR_ZBUF_CORNERS];
    rv_zbuf_point point[RV_EDITOR_ZBUF_CORNERS];
    int64_t area2 = 0;
    const rv_editor_zbuf_texture *texture = nullptr;
    rv_zbuf_texel texel;
    int32_t depth = 0;
};

rv_zbuf_point point_of(int32_t x, int32_t y)
{
    return { RV_EDITOR_ZBUF_DOUBLING * x, RV_EDITOR_ZBUF_DOUBLING * y };
}

// The console's edge function: E(p) = (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x). Affine in p,
// and twice the signed area of (a, b, p). Exact in int64 for corners within RV_EDITOR_ZBUF_COORD_LIMIT.
int64_t edge_at(rv_zbuf_point a, rv_zbuf_point b, rv_zbuf_point p)
{
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}

// Top-left rule, as the console's is_top_left: a top edge is horizontal with the interior below it,
// a left edge has the interior to its right. Only these cover a centre lying exactly on them, so a
// shared edge (traversed in opposite directions by its two triangles) is covered by exactly one.
bool is_top_left(int64_t dx, int64_t dy)
{
    return dy > 0 || (dy == 0 && dx < 0);
}

// Covered: strictly inside, or on a top-left edge.
bool covers(int64_t edge, int64_t dx, int64_t dy)
{
    return edge > 0 || (edge == 0 && is_top_left(dx, dy));
}

bool corner_valid(const rv_editor_zbuf_vertex &corner)
{
    const bool in_range = std::abs(static_cast<int64_t>(corner.x)) <= RV_EDITOR_ZBUF_COORD_LIMIT &&
        std::abs(static_cast<int64_t>(corner.y)) <= RV_EDITOR_ZBUF_COORD_LIMIT;
    return in_range;
}

// Logs once why a corner was refused: x or y beyond the coordinate limit. Logged once per process; the per-frame
// count in rv_editor_draw_filled shows that it recurs.
void report_corner_refusal()
{
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    std::fprintf(stderr, "3dmppc-editor: z-buffer corner refused: x or y beyond RV_EDITOR_ZBUF_COORD_LIMIT\n");
}

// Logs once why a textured triangle was refused: a texel coordinate above RV_EDITOR_ZBUF_TEXEL_MAX.
// Logged once per process; the per-frame count in rv_editor_draw_filled shows that it recurs.
void report_texel_refusal()
{
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    std::fprintf(stderr, "3dmppc-editor: z-buffer triangle refused: a texel coordinate is above RV_EDITOR_ZBUF_TEXEL_MAX\n");
}

// Logs once why a draw was refused for its setup: the buffer is not sized, or a texture has no texels or a
// non-positive size. Logged once per process, as the corner refusal is.
void report_setup_refusal()
{
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    std::fprintf(stderr,
        "3dmppc-editor: z-buffer draw refused: the buffer is not sized, or a texture has no texels or a non-positive size\n");
}

// Logs once why a buffer was not sized: a non-positive width or height.
void report_size_refusal()
{
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    std::fprintf(stderr, "3dmppc-editor: z-buffer not sized: width and height must be positive\n");
}

bool texture_valid(const rv_editor_zbuf_texture *texture)
{
    return texture == nullptr || (texture->texels != nullptr && texture->width > 0 && texture->height > 0);
}

size_t pixel_count(int width, int height)
{
    return static_cast<size_t>(width) * static_cast<size_t>(height);
}

// Floor of n / d for d > 0, as the console's floor_div: integer division truncates toward zero.
int64_t floor_div(int64_t n, int64_t d)
{
    const int64_t q = n / d;
    return n % d < 0 ? q - 1 : q;
}

// The console's texel_uvs: uv * size in 4.12 texels, moved by whole periods so the smallest corner lies in
// [0, period), then shifted to whole texels. RV_ERR_INVAL when a corner's texel is above RV_EDITOR_ZBUF_TEXEL_MAX.
int corner_texels(const rv_zbuf_tri &tri, rv_zbuf_texel &out)
{
    const rv_editor_zbuf_texture &texture = *tri.texture;
    const int64_t period_u = static_cast<int64_t>(texture.width) << RV_EDITOR_ZBUF_UV_FRAC_BITS;
    const int64_t period_v = static_cast<int64_t>(texture.height) << RV_EDITOR_ZBUF_UV_FRAC_BITS;
    int64_t raw_u[RV_EDITOR_ZBUF_CORNERS] = {};
    int64_t raw_v[RV_EDITOR_ZBUF_CORNERS] = {};
    for (int k = 0; k < RV_EDITOR_ZBUF_CORNERS; ++k) {
        raw_u[k] = static_cast<int64_t>(tri.corner[k].u) * texture.width;
        raw_v[k] = static_cast<int64_t>(tri.corner[k].v) * texture.height;
    }
    const int64_t shift_u = -floor_div(*std::min_element(std::begin(raw_u), std::end(raw_u)), period_u) * period_u;
    const int64_t shift_v = -floor_div(*std::min_element(std::begin(raw_v), std::end(raw_v)), period_v) * period_v;
    for (int k = 0; k < RV_EDITOR_ZBUF_CORNERS; ++k) {
        out.u[k] = (raw_u[k] + shift_u) >> RV_EDITOR_ZBUF_UV_FRAC_BITS;
        out.v[k] = (raw_v[k] + shift_v) >> RV_EDITOR_ZBUF_UV_FRAC_BITS;
        if (out.u[k] > RV_EDITOR_ZBUF_TEXEL_MAX || out.v[k] > RV_EDITOR_ZBUF_TEXEL_MAX) {
            return RV_ERR_INVAL;
        }
    }
    return RV_OK;
}

// The console's setup_uv: a texel coordinate is affine in screen space, the weight of corner j being the edge
// opposite it over area2. Its steps per pixel, in 16.16, are the edge's dy (x) and -dx (y). Numerators stay
// below 2^47 because texels are at most RV_EDITOR_ZBUF_TEXEL_MAX and corners at most 2^29 apart.
void texel_gradient(const rv_zbuf_tri &tri, const int64_t (&t)[RV_EDITOR_ZBUF_CORNERS], int64_t &dx, int64_t &dy)
{
    int64_t num_x = 0;
    int64_t num_y = 0;
    for (int j = 0; j < RV_EDITOR_ZBUF_CORNERS; ++j) {
        // The edge opposite corner j runs from corner j + 1 to corner j + 2.
        const int a = (j + 1) % RV_EDITOR_ZBUF_CORNERS;
        const int b = (j + 2) % RV_EDITOR_ZBUF_CORNERS;
        num_x += (static_cast<int64_t>(tri.corner[b].y) - tri.corner[a].y) * t[j];
        num_y -= (static_cast<int64_t>(tri.corner[b].x) - tri.corner[a].x) * t[j];
    }
    // num is in pixel units, so the divisor is the pixel area2 (doubled area2 / 4); undivided, every gradient would be
    // four times too small. Invariant: area2 > 0 here, since the flat-area case returns before any texel setup.
    const int64_t area2 = tri.area2 / (RV_EDITOR_ZBUF_DOUBLING * RV_EDITOR_ZBUF_DOUBLING);
    dx = std::clamp(num_x * RV_EDITOR_ZBUF_TEXEL_ONE / area2, -RV_EDITOR_ZBUF_GRADIENT_LIMIT, RV_EDITOR_ZBUF_GRADIENT_LIMIT);
    dy = std::clamp(num_y * RV_EDITOR_ZBUF_TEXEL_ONE / area2, -RV_EDITOR_ZBUF_GRADIENT_LIMIT, RV_EDITOR_ZBUF_GRADIENT_LIMIT);
}

// The texel setup of a textured triangle. RV_ERR_INVAL when the triangle does not fit (see corner_texels).
int texel_setup(rv_zbuf_tri &tri)
{
    const int rc = corner_texels(tri, tri.texel);
    if (rc != RV_OK) {
        return rc;
    }
    texel_gradient(tri, tri.texel.u, tri.texel.du_dx, tri.texel.du_dy);
    texel_gradient(tri, tri.texel.v, tri.texel.dv_dx, tri.texel.dv_dy);
    return RV_OK;
}

// TILE, as rv_pctexel::wrap: the texel integer wraps into one period; floor-mod, so a negative one wraps too.
int wrap_tile(int64_t coord, int size)
{
    const int64_t m = coord % size;
    return static_cast<int>(m < 0 ? m + size : m);
}

// Bytes per texel in a texture: RGBA8.
constexpr size_t RV_EDITOR_ZBUF_TEXEL_BYTES = 4;

// Read byte by byte: the texture's bytes are not rv_editor_zbuf_rgba objects, so no reinterpret.
rv_editor_zbuf_rgba texel_at(const rv_editor_zbuf_texture &texture, int64_t tx, int64_t ty)
{
    const size_t texel = static_cast<size_t>(wrap_tile(ty, texture.height)) * static_cast<size_t>(texture.width) +
        static_cast<size_t>(wrap_tile(tx, texture.width));
    const uint8_t *p = texture.texels + texel * RV_EDITOR_ZBUF_TEXEL_BYTES;
    return { p[0], p[1], p[2], p[3] };
}

// A product of two colour values, rounded half up to a byte: (a * b + 127) / 255.
uint8_t scale_byte(int64_t a, int64_t b)
{
    return static_cast<uint8_t>((a * b + RV_EDITOR_ZBUF_BYTE_MAX / 2) / RV_EDITOR_ZBUF_BYTE_MAX);
}

// One channel of a pixel: the corner values c mixed by the covered edge values w (each >= 0, summing to area2),
// that is sum(w * c) / area2 rounded half up. A wide area2 is shifted down first, so the sum stays in int64.
uint8_t mix_channel(const int64_t (&c)[RV_EDITOR_ZBUF_CORNERS], const int64_t (&w)[RV_EDITOR_ZBUF_CORNERS], int64_t area2)
{
    const int shift = std::max(0, static_cast<int>(std::bit_width(static_cast<uint64_t>(area2))) - RV_EDITOR_ZBUF_MIX_BITS);
    const int64_t den = area2 >> shift;
    int64_t sum = 0;
    for (int j = 0; j < RV_EDITOR_ZBUF_CORNERS; ++j) {
        sum += (w[j] >> shift) * c[j];
    }
    return static_cast<uint8_t>((sum + den / 2) / den);
}

// The interpolated colour of a pixel, all four channels, from its covered edge values w.
rv_editor_zbuf_rgba mix_colour(const rv_zbuf_tri &tri, const int64_t (&w)[RV_EDITOR_ZBUF_CORNERS])
{
    const int64_t red[RV_EDITOR_ZBUF_CORNERS] = { tri.corner[0].r, tri.corner[1].r, tri.corner[2].r };
    const int64_t green[RV_EDITOR_ZBUF_CORNERS] = { tri.corner[0].g, tri.corner[1].g, tri.corner[2].g };
    const int64_t blue[RV_EDITOR_ZBUF_CORNERS] = { tri.corner[0].b, tri.corner[1].b, tri.corner[2].b };
    const int64_t alpha[RV_EDITOR_ZBUF_CORNERS] = { tri.corner[0].a, tri.corner[1].a, tri.corner[2].a };
    return { mix_channel(red, w, tri.area2),
        mix_channel(green, w, tri.area2),
        mix_channel(blue, w, tri.area2),
        mix_channel(alpha, w, tri.area2) };
}

// The texel under pixel (x, y) of a textured triangle, as the console's shade_textured: u_fx >> 16 at the pixel.
// The texel is affine on screen: the base at corner 0 plus the steps.
rv_editor_zbuf_rgba textured_texel(const rv_zbuf_tri &tri, int x, int y)
{
    const int64_t off_x = static_cast<int64_t>(x) - tri.corner[0].x;
    const int64_t off_y = static_cast<int64_t>(y) - tri.corner[0].y;
    const int64_t u_fx = tri.texel.u[0] * RV_EDITOR_ZBUF_TEXEL_ONE + off_x * tri.texel.du_dx + off_y * tri.texel.du_dy +
        (tri.texel.du_dx >> RV_EDITOR_ZBUF_HALF_STEP_SHIFT) + (tri.texel.du_dy >> RV_EDITOR_ZBUF_HALF_STEP_SHIFT);
    const int64_t v_fx = tri.texel.v[0] * RV_EDITOR_ZBUF_TEXEL_ONE + off_x * tri.texel.dv_dx + off_y * tri.texel.dv_dy +
        (tri.texel.dv_dx >> RV_EDITOR_ZBUF_HALF_STEP_SHIFT) + (tri.texel.dv_dy >> RV_EDITOR_ZBUF_HALF_STEP_SHIFT);
    return texel_at(*tri.texture, u_fx >> RV_EDITOR_ZBUF_TEXEL_FRAC, v_fx >> RV_EDITOR_ZBUF_TEXEL_FRAC);
}

// The edge values of a counter-clockwise triangle at a sample: edge[i] is corner i's weight. True when all three
// edges cover the sample.
bool edges_cover(const rv_zbuf_point (&point)[RV_EDITOR_ZBUF_CORNERS],
    rv_zbuf_point sample,
    int64_t (&edge)[RV_EDITOR_ZBUF_CORNERS])
{
    for (int i = 0; i < RV_EDITOR_ZBUF_CORNERS; ++i) {
        // The edge opposite corner i runs from corner i + 1 to corner i + 2.
        const int a = (i + 1) % RV_EDITOR_ZBUF_CORNERS;
        const int b = (i + 2) % RV_EDITOR_ZBUF_CORNERS;
        edge[i] = edge_at(point[a], point[b], sample);
        if (!covers(edge[i], point[b].x - point[a].x, point[b].y - point[a].y)) {
            return false;
        }
    }
    return true;
}

// One pixel centre: covered by the triangle, with a key not below the one stored there, and with a
// non-zero output alpha. Only then the colour and key are written.
// ponytail: the three edge functions are evaluated for every pixel of the bounding box, no
// stepping along the edges. Ceiling not measured; upgrade: incremental edge stepping per row.
void shade_pixel(rv_editor_zbuf &buf, const rv_zbuf_tri &tri, int x, int y)
{
    // The centre of pixel (x, y) in doubled units.
    const rv_zbuf_point centre = { RV_EDITOR_ZBUF_DOUBLING * x + RV_EDITOR_ZBUF_CENTRE_OFFSET,
        RV_EDITOR_ZBUF_DOUBLING * y + RV_EDITOR_ZBUF_CENTRE_OFFSET };
    int64_t edge[RV_EDITOR_ZBUF_CORNERS] = {};
    if (!edges_cover(tri.point, centre, edge)) {
        return;
    }

    const size_t index = static_cast<size_t>(y) * static_cast<size_t>(buf.width) + static_cast<size_t>(x);
    // Mirrors depth_accept (src/rv_pconsole/cv/rv_pcfbuf.hpp): a key below the stored one is refused, an equal one passes.
    if (tri.depth < buf.depth[index]) {
        return;
    }

    // The colour modulates the texel: an editor lighting rule (tint, shade). The console's shade_textured emits the
    // texel alone; for a flat pixel the texel is white and the colour is the pixel.
    const rv_editor_zbuf_rgba colour = mix_colour(tri, edge);
    const rv_editor_zbuf_rgba texel = tri.texture != nullptr ? textured_texel(tri, x, y) : RV_EDITOR_ZBUF_FLAT_TEXEL;
    const rv_editor_zbuf_rgba out = { scale_byte(colour.r, texel.r),
        scale_byte(colour.g, texel.g),
        scale_byte(colour.b, texel.b),
        scale_byte(colour.a, texel.a) };
    if (out.a == 0) {
        return;
    }
    // Mirrors depth_store (src/rv_pconsole/cv/rv_pcfbuf.hpp).
    buf.depth[index] = tri.depth;
    buf.colour[index] = out;
}

// The triangle's bounding box in pixels, clipped to the buffer: the console's clip_bounds. Pixels
// outside it are never covered. False when the box is empty.
bool clip_bounds(const rv_zbuf_tri &tri, int width, int height, int64_t &min_x, int64_t &min_y, int64_t &max_x, int64_t &max_y)
{
    const int64_t xs[] = { tri.corner[0].x, tri.corner[1].x, tri.corner[2].x };
    const int64_t ys[] = { tri.corner[0].y, tri.corner[1].y, tri.corner[2].y };
    min_x = std::max<int64_t>(*std::min_element(std::begin(xs), std::end(xs)), 0);
    max_x = std::min<int64_t>(*std::max_element(std::begin(xs), std::end(xs)), width - 1);
    min_y = std::max<int64_t>(*std::min_element(std::begin(ys), std::end(ys)), 0);
    max_y = std::min<int64_t>(*std::max_element(std::begin(ys), std::end(ys)), height - 1);
    return min_x <= max_x && min_y <= max_y;
}

} // namespace

int rv_editor_zbuf_init(rv_editor_zbuf &buf, int width, int height)
{
    if (width <= 0 || height <= 0) {
        report_size_refusal();
        return RV_ERR_INVAL;
    }
    buf.width = width;
    buf.height = height;
    buf.colour.assign(pixel_count(width, height), rv_editor_zbuf_rgba{});
    buf.depth.assign(pixel_count(width, height), RV_EDITOR_ZBUF_DEPTH_FARTHEST);
    return RV_OK;
}

void rv_editor_zbuf_clear(rv_editor_zbuf &buf, rv_editor_zbuf_rgba background)
{
    std::fill(buf.colour.begin(), buf.colour.end(), background);
    std::fill(buf.depth.begin(), buf.depth.end(), RV_EDITOR_ZBUF_DEPTH_FARTHEST);
}

int rv_editor_zbuf_draw(rv_editor_zbuf &buf, const rv_editor_zbuf_triangle &tri)
{
    const bool sized = buf.width > 0 && buf.height > 0 && buf.colour.size() == pixel_count(buf.width, buf.height) &&
        buf.depth.size() == buf.colour.size();
    if (!std::all_of(std::begin(tri.corner), std::end(tri.corner), corner_valid)) {
        report_corner_refusal();
        return RV_ERR_INVAL;
    }
    if (!sized || !texture_valid(tri.texture)) {
        report_setup_refusal();
        return RV_ERR_INVAL;
    }

    rv_zbuf_tri prepared;
    for (int i = 0; i < RV_EDITOR_ZBUF_CORNERS; ++i) {
        prepared.corner[i] = tri.corner[i];
        prepared.point[i] = point_of(tri.corner[i].x, tri.corner[i].y);
    }
    // Normalised winding, as normalize_winding: the swap keeps every edge function's sense the same.
    prepared.area2 = edge_at(prepared.point[0], prepared.point[1], prepared.point[2]);
    if (prepared.area2 < 0) {
        std::swap(prepared.corner[1], prepared.corner[2]);
        std::swap(prepared.point[1], prepared.point[2]);
        prepared.area2 = -prepared.area2;
    }
    prepared.texture = tri.texture;
    prepared.depth = tri.depth;
    if (prepared.area2 == 0) {
        return RV_OK;
    }
    if (prepared.texture != nullptr && texel_setup(prepared) != RV_OK) {
        report_texel_refusal();
        return RV_ERR_INVAL;
    }

    int64_t min_x = 0;
    int64_t min_y = 0;
    int64_t max_x = 0;
    int64_t max_y = 0;
    if (!clip_bounds(prepared, buf.width, buf.height, min_x, min_y, max_x, max_y)) {
        return RV_OK;
    }
    for (int64_t y = min_y; y <= max_y; ++y) {
        for (int64_t x = min_x; x <= max_x; ++x) {
            shade_pixel(buf, prepared, static_cast<int>(x), static_cast<int>(y));
        }
    }
    return RV_OK;
}

bool rv_editor_zbuf_covers(const int32_t (&x)[RV_EDITOR_ZBUF_CORNERS],
    const int32_t (&y)[RV_EDITOR_ZBUF_CORNERS],
    int64_t sample_x,
    int64_t sample_y)
{
    rv_zbuf_point point[RV_EDITOR_ZBUF_CORNERS] = {};
    for (int k = 0; k < RV_EDITOR_ZBUF_CORNERS; ++k) {
        point[k] = point_of(x[k], y[k]);
    }
    const int64_t area2 = edge_at(point[0], point[1], point[2]);
    if (area2 == 0) {
        return false;
    }
    // Normalised winding, as draw: the swap keeps every edge function's sense the same.
    if (area2 < 0) {
        std::swap(point[1], point[2]);
    }
    int64_t edge[RV_EDITOR_ZBUF_CORNERS] = {};
    return edges_cover(point, { sample_x, sample_y }, edge);
}

} // namespace rv_editor
