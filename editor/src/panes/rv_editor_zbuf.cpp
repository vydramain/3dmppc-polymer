// CPU z-buffer rasteriser. Coverage mirrors src/rv_pconsole/cv/rv_pcraster_poly.cpp in int64: edge_at,
// is_top_left and clip_bounds, and normalize_winding inline in rv_editor_zbuf_draw. Samples are pixel
// centres, not the console's corners (decision D4): the corners are doubled, so a centre is an integer. Texel alpha 0 is the
// console's transparent texel 0000h (pdk/include/pdk/cv/rv_texel.h); the TILE nearest-texel wrap follows rv_pctexel::wrap.

#include "panes/rv_editor_zbuf.hpp"

#include <algorithm>
#include <cmath>
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

// Colour channels: 0..1 per vertex, 0..255 per texel and in the buffer.
constexpr double RV_EDITOR_ZBUF_CHANNEL_MAX = 255.0;

// Flat colour: an opaque white texel, so the vertex colour is the pixel colour.
constexpr rv_editor_zbuf_rgba RV_EDITOR_ZBUF_FLAT_TEXEL = { 255, 255, 255, 255 };

// Cleared depth: the farthest key, so the first triangle to touch a pixel always passes. Mirrors
// RV_PCCV_DEPTH_FARTHEST in src/rv_pconsole/cv/rv_pccv_sw.cpp.
constexpr int32_t RV_EDITOR_ZBUF_DEPTH_FARTHEST = std::numeric_limits<int32_t>::min();

// Corners are scaled by this, so a pixel's centre lies on an integer: the centre of pixel p is
// RV_EDITOR_ZBUF_DOUBLING * p + RV_EDITOR_ZBUF_CENTRE_OFFSET, half a pixel past its corner.
constexpr int64_t RV_EDITOR_ZBUF_DOUBLING = 2;
constexpr int64_t RV_EDITOR_ZBUF_CENTRE_OFFSET = 1;

// A point in doubled pixel units (RV_EDITOR_ZBUF_DOUBLING).
struct rv_zbuf_point {
    int64_t x = 0;
    int64_t y = 0;
};

// The corner values interpolated over a triangle, before the texel and the tint.
struct rv_zbuf_attr {
    double u = 0.0;
    double v = 0.0;
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 0.0;
};

// A triangle put in counter-clockwise order (area2 > 0), with its doubled points.
struct rv_zbuf_tri {
    rv_editor_zbuf_vertex corner[RV_EDITOR_ZBUF_CORNERS];
    rv_zbuf_point point[RV_EDITOR_ZBUF_CORNERS];
    int64_t area2 = 0;
    const rv_editor_zbuf_texture *texture = nullptr;
    int32_t depth = 0;
};

rv_zbuf_point point_of(const rv_editor_zbuf_vertex &corner)
{
    return { RV_EDITOR_ZBUF_DOUBLING * corner.x, RV_EDITOR_ZBUF_DOUBLING * corner.y };
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
    const float values[] = { corner.z, corner.u, corner.v, corner.r, corner.g, corner.b, corner.a };
    const bool in_range = std::abs(static_cast<int64_t>(corner.x)) <= RV_EDITOR_ZBUF_COORD_LIMIT &&
        std::abs(static_cast<int64_t>(corner.y)) <= RV_EDITOR_ZBUF_COORD_LIMIT;
    return in_range && corner.z > 0.0f && std::all_of(std::begin(values), std::end(values), [](float value) {
        return std::isfinite(value);
    });
}

// Logs once why a corner was refused: x or y beyond the coordinate limit, or else a value that is
// not finite or a z <= 0. Logged once per process; the per-frame count in rv_editor_draw_filled shows that it recurs.
void report_corner_refusal(const rv_editor_zbuf_triangle &tri)
{
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    const bool beyond = std::any_of(std::begin(tri.corner), std::end(tri.corner), [](const rv_editor_zbuf_vertex &corner) {
        return std::abs(static_cast<int64_t>(corner.x)) > RV_EDITOR_ZBUF_COORD_LIMIT ||
            std::abs(static_cast<int64_t>(corner.y)) > RV_EDITOR_ZBUF_COORD_LIMIT;
    });
    std::fprintf(stderr,
        "3dmppc-editor: z-buffer corner refused: %s\n",
        beyond ? "x or y beyond RV_EDITOR_ZBUF_COORD_LIMIT" : "a value is not finite or z <= 0");
}

bool texture_valid(const rv_editor_zbuf_texture *texture)
{
    return texture == nullptr || (texture->texels != nullptr && texture->width > 0 && texture->height > 0);
}

size_t pixel_count(int width, int height)
{
    return static_cast<size_t>(width) * static_cast<size_t>(height);
}

// TILE: the coordinate wraps into one period and the nearest texel is taken, as rv_pctexel::wrap does.
int texel_index(double coord, int size)
{
    const double period = static_cast<double>(size);
    double texel = std::fmod(coord * period, period);
    if (texel < 0.0) {
        texel += period;
    }
    return std::clamp(static_cast<int>(std::floor(texel)), 0, size - 1);
}

// Bytes per texel in a texture: RGBA8.
constexpr size_t RV_EDITOR_ZBUF_TEXEL_BYTES = 4;

// Read byte by byte: the texture's bytes are not rv_editor_zbuf_rgba objects, so no reinterpret.
rv_editor_zbuf_rgba texel_at(const rv_editor_zbuf_texture &texture, double u, double v)
{
    const int x = texel_index(u, texture.width);
    const int y = texel_index(v, texture.height);
    const size_t texel = static_cast<size_t>(y) * static_cast<size_t>(texture.width) + static_cast<size_t>(x);
    const uint8_t *p = texture.texels + texel * RV_EDITOR_ZBUF_TEXEL_BYTES;
    return { p[0], p[1], p[2], p[3] };
}

// A unit colour times a 0..255 texel channel, rounded to a byte.
uint8_t channel(double unit, uint8_t texel)
{
    return static_cast<uint8_t>(std::clamp(std::round(unit * texel), 0.0, RV_EDITOR_ZBUF_CHANNEL_MAX));
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
    double perspective[RV_EDITOR_ZBUF_CORNERS] = {};
    double inv_z = 0.0;
    for (int i = 0; i < RV_EDITOR_ZBUF_CORNERS; ++i) {
        // The edge opposite corner i runs from corner i + 1 to corner i + 2.
        const int a = (i + 1) % RV_EDITOR_ZBUF_CORNERS;
        const int b = (i + 2) % RV_EDITOR_ZBUF_CORNERS;
        const int64_t edge = edge_at(tri.point[a], tri.point[b], centre);
        if (!covers(edge, tri.point[b].x - tri.point[a].x, tri.point[b].y - tri.point[a].y)) {
            return;
        }
        // Screen-linear weight, divided by the corner depth: 1/z interpolates linearly on screen.
        perspective[i] = static_cast<double>(edge) / static_cast<double>(tri.area2) / tri.corner[i].z;
        inv_z += perspective[i];
    }

    const double z = 1.0 / inv_z;
    const size_t index = static_cast<size_t>(y) * static_cast<size_t>(buf.width) + static_cast<size_t>(x);
    // Mirrors depth_accept (src/rv_pconsole/cv/rv_pcfbuf.hpp): a key below the stored one is refused, an equal one passes.
    if (tri.depth < buf.depth[index]) {
        return;
    }

    // Normalised perspective weights: the attributes are mixed with them, not with screen weights.
    rv_zbuf_attr attr;
    for (int i = 0; i < RV_EDITOR_ZBUF_CORNERS; ++i) {
        const rv_editor_zbuf_vertex &corner = tri.corner[i];
        const double k = perspective[i] * z;
        attr.u += k * corner.u;
        attr.v += k * corner.v;
        attr.r += k * corner.r;
        attr.g += k * corner.g;
        attr.b += k * corner.b;
        attr.a += k * corner.a;
    }

    const rv_editor_zbuf_rgba texel = tri.texture ? texel_at(*tri.texture, attr.u, attr.v) : RV_EDITOR_ZBUF_FLAT_TEXEL;
    const rv_editor_zbuf_rgba out = { channel(attr.r, texel.r),
        channel(attr.g, texel.g),
        channel(attr.b, texel.b),
        channel(attr.a, texel.a) };
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
    const bool corners_ok = std::all_of(std::begin(tri.corner), std::end(tri.corner), corner_valid);
    if (!corners_ok) {
        report_corner_refusal(tri);
    }
    // Only a corner is logged: the caller sizes the target before drawing, and the decoder guarantees
    // a texture's w, h > 0, so the sized and texture checks cannot fail in practice.
    if (!sized || !corners_ok || !texture_valid(tri.texture)) {
        return RV_ERR_INVAL;
    }

    rv_zbuf_tri prepared;
    for (int i = 0; i < RV_EDITOR_ZBUF_CORNERS; ++i) {
        prepared.corner[i] = tri.corner[i];
        prepared.point[i] = point_of(tri.corner[i]);
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

} // namespace rv_editor
