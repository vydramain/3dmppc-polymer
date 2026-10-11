#pragma once

#include <cstdint>
#include <vector>

// CPU z-buffer for the Scene viewport: triangles go into an RGBA8 colour buffer and a float
// depth buffer. Plain data, no ImGui or SDL, so the rasteriser is checked on its own.

namespace rv_editor
{

// A triangle has three corners.
constexpr int RV_EDITOR_ZBUF_CORNERS = 3;

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

// One corner. x, y are pixels; z is view depth and must be > 0; u, v are glTF uv, repeated
// (TILE); r, g, b, a are 0..1.
struct rv_editor_zbuf_vertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 1.0f;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct rv_editor_zbuf_triangle {
    rv_editor_zbuf_vertex corner[RV_EDITOR_ZBUF_CORNERS];
    // Null: the triangle is flat-coloured.
    const rv_editor_zbuf_texture *texture = nullptr;
};

struct rv_editor_zbuf {
    int width = 0;
    int height = 0;
    std::vector<rv_editor_zbuf_rgba> colour;
    std::vector<float> depth;
};

// Sizes the buffer. RV_ERR_INVAL for a non-positive size.
int rv_editor_zbuf_init(rv_editor_zbuf &buf, int width, int height);

// Colour becomes `background`, depth becomes infinity (nothing drawn yet).
void rv_editor_zbuf_clear(rv_editor_zbuf &buf, rv_editor_zbuf_rgba background = {});

// Rasterises one triangle. A nearer pixel wins; on equal depth the pixel drawn first stays, so
// draw order matters only for equal depths. A pixel whose output alpha is 0 writes neither
// colour nor depth. Winding is not culled.
// RV_ERR_INVAL, nothing drawn, for: an uninitialised buffer; a corner with non-finite values or
// z <= 0; a texture without texels or with a non-positive size.
int rv_editor_zbuf_draw(rv_editor_zbuf &buf, const rv_editor_zbuf_triangle &tri);

} // namespace rv_editor
