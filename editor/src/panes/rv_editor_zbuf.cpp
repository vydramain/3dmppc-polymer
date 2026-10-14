// CPU z-buffer rasteriser. Coverage and the top-left rule follow src/rv_pconsole/cv/rv_pcraster_poly.cpp.
// Texel alpha 0 is the console's transparent texel 0000h (pdk/include/pdk/cv/rv_texel.h); the
// TILE nearest-texel wrap follows rv_pctexel::wrap.

#include "panes/rv_editor_zbuf.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <utility>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// A pixel is sampled at its centre, half a pixel past its integer corner.
constexpr double RV_EDITOR_ZBUF_PIXEL_CENTRE = 0.5;

// Colour channels: 0..1 per vertex, 0..255 per texel and in the buffer.
constexpr double RV_EDITOR_ZBUF_CHANNEL_MAX = 255.0;

// Flat colour: an opaque white texel, so the vertex colour is the pixel colour.
constexpr rv_editor_zbuf_rgba RV_EDITOR_ZBUF_FLAT_TEXEL = { 255, 255, 255, 255 };

struct rv_zbuf_point {
    double x = 0.0;
    double y = 0.0;
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

// A triangle put in counter-clockwise screen order (area2 > 0), with its points.
struct rv_zbuf_tri {
    rv_editor_zbuf_vertex corner[RV_EDITOR_ZBUF_CORNERS];
    rv_zbuf_point point[RV_EDITOR_ZBUF_CORNERS];
    double area2 = 0.0;
    const rv_editor_zbuf_texture *texture = nullptr;
};

rv_zbuf_point point_of(const rv_editor_zbuf_vertex &corner)
{
    return { corner.x, corner.y };
}

// Twice the signed area of (a, b, p): cross(b - a, p - a). The endpoints are taken in a fixed
// order, so two triangles sharing an edge get the same magnitude bit for bit.
double edge_at(rv_zbuf_point a, rv_zbuf_point b, rv_zbuf_point p)
{
    const bool swapped = b.x < a.x || (b.x == a.x && b.y < a.y);
    const rv_zbuf_point first = swapped ? b : a;
    const rv_zbuf_point second = swapped ? a : b;
    const double value = (second.x - first.x) * (p.y - first.y) - (second.y - first.y) * (p.x - first.x);
    return swapped ? -value : value;
}

// Top-left rule, y grows downwards. A top edge is horizontal with the interior below it; a left
// edge has the interior to its right, and every edge running upwards (dy < 0) is one. Only
// these cover a centre lying exactly on them. A shared edge runs the other way in its second
// triangle, so exactly one of the two covers it.
bool covers(double edge, double dx, double dy)
{
    return edge > 0.0 || (edge == 0.0 && (dy < 0.0 || (dy == 0.0 && dx > 0.0)));
}

bool corner_valid(const rv_editor_zbuf_vertex &corner)
{
    const float values[] = { corner.x, corner.y, corner.z, corner.u, corner.v, corner.r, corner.g, corner.b, corner.a };
    return corner.z > 0.0f && std::all_of(std::begin(values), std::end(values), [](float value) {
        return std::isfinite(value);
    });
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

// One pixel centre: covered by the triangle, nearer than the depth stored there, and with a
// non-zero output alpha. Only then the colour and depth are written.
// ponytail: the three edge functions are evaluated for every pixel of the bounding box, no
// stepping along the edges. Ceiling not measured; upgrade: incremental edge stepping per row.
void shade_pixel(rv_editor_zbuf &buf, const rv_zbuf_tri &tri, int x, int y)
{
    const rv_zbuf_point centre = { x + RV_EDITOR_ZBUF_PIXEL_CENTRE, y + RV_EDITOR_ZBUF_PIXEL_CENTRE };
    double perspective[RV_EDITOR_ZBUF_CORNERS] = {};
    double inv_z = 0.0;
    for (int i = 0; i < RV_EDITOR_ZBUF_CORNERS; ++i) {
        // The edge opposite corner i runs from corner i + 1 to corner i + 2.
        const int a = (i + 1) % RV_EDITOR_ZBUF_CORNERS;
        const int b = (i + 2) % RV_EDITOR_ZBUF_CORNERS;
        const double edge = edge_at(tri.point[a], tri.point[b], centre);
        if (!covers(edge, tri.point[b].x - tri.point[a].x, tri.point[b].y - tri.point[a].y)) {
            return;
        }
        // Screen-linear weight, divided by the corner depth: 1/z interpolates linearly on screen.
        perspective[i] = edge / tri.area2 / tri.corner[i].z;
        inv_z += perspective[i];
    }

    const double z = 1.0 / inv_z;
    const size_t index = static_cast<size_t>(y) * static_cast<size_t>(buf.width) + static_cast<size_t>(x);
    if (!(z < buf.depth[index])) {
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
    buf.depth[index] = static_cast<float>(z);
    buf.colour[index] = out;
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
    buf.depth.assign(pixel_count(width, height), std::numeric_limits<float>::infinity());
    return RV_OK;
}

void rv_editor_zbuf_clear(rv_editor_zbuf &buf, rv_editor_zbuf_rgba background)
{
    std::fill(buf.colour.begin(), buf.colour.end(), background);
    std::fill(buf.depth.begin(), buf.depth.end(), std::numeric_limits<float>::infinity());
}

int rv_editor_zbuf_draw(rv_editor_zbuf &buf, const rv_editor_zbuf_triangle &tri)
{
    const bool sized = buf.width > 0 && buf.height > 0 && buf.colour.size() == pixel_count(buf.width, buf.height) &&
        buf.depth.size() == buf.colour.size();
    const bool corners_ok = std::all_of(std::begin(tri.corner), std::end(tri.corner), corner_valid);
    if (!sized || !corners_ok || !texture_valid(tri.texture)) {
        return RV_ERR_INVAL;
    }

    rv_zbuf_tri prepared;
    for (int i = 0; i < RV_EDITOR_ZBUF_CORNERS; ++i) {
        prepared.corner[i] = tri.corner[i];
        prepared.point[i] = point_of(tri.corner[i]);
    }
    prepared.area2 = edge_at(prepared.point[0], prepared.point[1], prepared.point[2]);
    if (prepared.area2 < 0.0) {
        std::swap(prepared.corner[1], prepared.corner[2]);
        std::swap(prepared.point[1], prepared.point[2]);
        prepared.area2 = -prepared.area2;
    }
    prepared.texture = tri.texture;
    if (prepared.area2 == 0.0) {
        return RV_OK;
    }

    double min_x = prepared.point[0].x;
    double max_x = min_x;
    double min_y = prepared.point[0].y;
    double max_y = min_y;
    for (int i = 1; i < RV_EDITOR_ZBUF_CORNERS; ++i) {
        min_x = std::min(min_x, prepared.point[i].x);
        max_x = std::max(max_x, prepared.point[i].x);
        min_y = std::min(min_y, prepared.point[i].y);
        max_y = std::max(max_y, prepared.point[i].y);
    }

    // Outside the buffer: no centre can lie inside. Checked before any conversion to int, so
    // coordinates far beyond the buffer never overflow it.
    const double last_x = buf.width - 1.0;
    const double last_y = buf.height - 1.0;
    if (max_x < 0.0 || max_y < 0.0 || min_x > last_x || min_y > last_y) {
        return RV_OK;
    }

    // The pixel rectangle that can hold a centre inside the triangle, clipped to the buffer.
    const int x0 = static_cast<int>(std::max(0.0, std::floor(min_x)));
    const int x1 = static_cast<int>(std::min(last_x, std::ceil(max_x)));
    const int y0 = static_cast<int>(std::max(0.0, std::floor(min_y)));
    const int y1 = static_cast<int>(std::min(last_y, std::ceil(max_y)));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            shade_pixel(buf, prepared, x, y);
        }
    }
    return RV_OK;
}

} // namespace rv_editor
