#pragma once

#include <cstdint>
#include <vector>

// CPU z-buffer for the Scene viewport: triangles go into an RGBA8 colour buffer and an int32
// depth buffer of ordering keys (larger = nearer, as the console's). Plain data, no ImGui or SDL,
// so the rasteriser is checked on its own.

namespace rv_editor
{

// A triangle has three corners.
constexpr int RV_EDITOR_ZBUF_CORNERS = 3;

// Largest |x| or |y| of a corner, in buffer pixels. Above it the int64 edge products could overflow:
// doubled coordinates and centres are up to 2^29 + 1, differences up to 2^30 + 1, and each product
// stays below 2^61.
constexpr int64_t RV_EDITOR_ZBUF_COORD_LIMIT = int64_t{ 1 } << 28;

struct rv_editor_zbuf_rgba {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

// A CPU copy of a texture: RGBA8 bytes, four per texel, row-major from the top; the caller owns them.
struct rv_editor_zbuf_texture {
    const uint8_t *texels = nullptr;
    int width = 0;
    int height = 0;
};

// The model's raw uv is int16 4.12 (its uv_bits): RV_EDITOR_ZBUF_UV_ONE is one texture.
constexpr int RV_EDITOR_ZBUF_UV_FRAC_BITS = 12;
constexpr int32_t RV_EDITOR_ZBUF_UV_ONE = int32_t{ 1 } << RV_EDITOR_ZBUF_UV_FRAC_BITS;

// One corner. x, y are buffer pixels, integers as rv_gte_project gives them; u, v are the model's raw int16 4.12
// uv, repeated (TILE); r, g, b, a are colour values 0..255. The depth is the triangle's key, not a corner's z.
struct rv_editor_zbuf_vertex {
    int32_t x = 0;
    int32_t y = 0;
    int32_t u = 0;
    int32_t v = 0;
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

struct rv_editor_zbuf_triangle {
    rv_editor_zbuf_vertex corner[RV_EDITOR_ZBUF_CORNERS];
    // Null: the triangle is flat-coloured.
    const rv_editor_zbuf_texture *texture = nullptr;
    // The triangle's ordering key, one per primitive (rv_gte_depth_key): larger is nearer. The default 0 is a
    // placeholder; the caller must set it.
    int32_t depth = 0;
};

struct rv_editor_zbuf {
    int width = 0;
    int height = 0;
    std::vector<rv_editor_zbuf_rgba> colour;
    std::vector<int32_t> depth;
};

// Sizes the buffer. RV_ERR_INVAL for a non-positive size.
int rv_editor_zbuf_init(rv_editor_zbuf &buf, int width, int height);

// Colour becomes `background`, depth becomes the farthest key (nothing drawn yet).
void rv_editor_zbuf_clear(rv_editor_zbuf &buf, rv_editor_zbuf_rgba background = {});

// Rasterises one triangle. A pixel keeps the larger key: a triangle draws over a pixel whose stored key
// is not larger than its own, so on equal keys the later triangle wins, as the console's depth_accept. A pixel whose output
// alpha is 0 writes neither colour nor depth. Winding is not culled. RV_ERR_INVAL, nothing drawn, for: an uninitialised buffer;
// a corner with x or y beyond RV_EDITOR_ZBUF_COORD_LIMIT; a texture without texels or with a
// non-positive size.
int rv_editor_zbuf_draw(rv_editor_zbuf &buf, const rv_editor_zbuf_triangle &tri);

} // namespace rv_editor
