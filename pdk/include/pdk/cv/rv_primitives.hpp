#pragma once

#include <cstdint>

#include "pdk/cv/rv_vertex.hpp"

namespace rv_3dmppc {

// A line segment handed to rv_cv::frame_put() inside an rv_primitive. The two
// vertex colours are interpolated along the segment (equal colours = a flat
// line); the uv fields are ignored — lines are never textured. There is no
// polyline entity: a chain is simply N line primitives (the PSX polyline existed
// only because of its word-by-word command bus).
struct rv_line {
    rv_vertex vertexes[2];
};

// How the inside of a primitive is filled. A tag, not a bitmask — the modes are
// mutually exclusive.
//
//   FLAT_COLOURED   colour only, no texture: a polygon interpolates its vertex
//                   colours across the surface (equal colours = flat, different
//                   = gouraud — no separate shading flag is needed), a sprite
//                   fills with its single `color`.
//   SAMPLE_TEXTURE  fill by sampling the referenced texture.
//   WIREFRAME       draw only the edges, not the interior: polygon edges use
//                   the vertex colours, interpolated along each edge (edge A-B
//                   shades from A's colour to B's colour, like a line
//                   primitive); a sprite outline uses its single `color`.
//
// New modes take a new enumerator; existing values are ABI.
enum rv_primitive_fill_mode {
    RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED = 1,
    RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE = 2,
    RV_PRIMITIVE_FILL_MODE_WIREFRAME = 3,
};

// What happens when a texture coordinate falls outside the texture. A
// per-primitive DRAWING attribute (the same uploaded texture may be clamped on
// one polygon and tiled on another), not a property of the stored texels.
//
// New modes take a new enumerator; existing values are ABI.
enum rv_texture_mapping_type {
    RV_TEXWRAP_CLAMP = 1,    // clamp to the edge texel
    RV_TEXWRAP_TILE = 2,     // repeat
    RV_TEXWRAP_STRETCH = 3,  // stretch the texture across the primitive
};

// A triangle or a quad handed to rv_cv::frame_put() inside an rv_primitive.
// Self-contained: it carries its own geometry AND every attribute needed to draw
// it, because the console re-orders primitives by depth (see rv_primitive) —
// there is no global "current texture / current state" a polygon could rely on.
//
// A quad (vertex_count == 4) is rendered as two triangles, (1,2,3) and (2,3,4);
// the split affects how colours and uv are interpolated, so vertex order matters
// (PSX rule, kept on purpose).
//
// The texture and palette are referenced by the addresses
// rv_cv::video_asset_malloc returned; both are read only when fill_mode samples
// the texture, and addr_palette only for the indexed rv_texfmt formats.
//
// DEFERRED: blending (opaque/semi-transparent) and texture-combine
// (raw/modulation) flags.
struct rv_polygon {
    uint32_t fill_mode;  // rv_primitive_fill_mode

    int64_t addr_texture;             // texture data in video RAM (if sampling)
    int64_t addr_palette;             // palette in video RAM (indexed formats)
    rv_texture_mapping_type mapping;  // how to sample outside the texture

    uint32_t vertex_count;  // 3 = triangle, 4 = quad; anything else = RV_ERR_INVAL
    rv_vertex vertexes[4];  // vertexes[3] is ignored when vertex_count == 3
};

// An axis-aligned rectangle ("sprite") handed to rv_cv::frame_put() inside an
// rv_primitive. Axis-aligned BY CONSTRUCTION — a corner plus a size can express
// no rotation (use a polygon for that) — and cheaper to draw than a polygon:
// one colour, no per-vertex interpolation, no triangle split.
//
// When fill_mode samples a texture it is applied over the rectangle from the
// texture's upper-left corner, per `mapping`; WIREFRAME outlines the rectangle
// with `color`. Addresses are read the same way as on rv_polygon.
//
// Off-screen portions are clipped by the console; the field widths promise
// nothing beyond the screen the game queried (rv_cv::screen_width/height).
//
// DEFERRED: the blending flags and the PSX fixed-size fast paths (1x1/8x8/16x16).
struct rv_sprite {
    uint32_t fill_mode;  // rv_primitive_fill_mode

    int64_t addr_texture;  // texture data in video RAM (if sampling)
    int64_t addr_palette;  // palette in video RAM (indexed formats)

    rv_color color;
    rv_texture_mapping_type mapping;  // how to sample outside the texture

    int16_t x, y;            // upper-left corner (signed: may start off-screen)
    uint16_t width, height;  // extent in pixels
};

// Which member of rv_primitive::data is active. Values become ABI when the first
// console ships; until then renumbering is free.
enum rv_primitive_type {
    RV_PRIMITIVE_LINE = 1,
    RV_PRIMITIVE_POLYGON = 2,
    RV_PRIMITIVE_SPRITE = 3,
};

// One thing to draw this frame, handed to rv_cv::frame_put(). The console does
// not draw it immediately: it files every primitive by `depth` and, at
// frame_flush(), renders them in ascending depth order (the ordering table). So
// `depth` is a sort key over the whole primitive, not a per-pixel Z — a whole
// primitive goes in front of or behind another.
//
// `depth` is signed and lives on the primitive itself, shared by every variant,
// because all variants are ordered together. The console's contract: a LARGER
// value is NEARER — drawn later, on top (background at small depth, HUD at
// large); at equal depth the submission order is kept (first put = drawn first =
// behind); out-of-range values clamp to the nearest bucket. The game hands a
// depth VALUE, never a bucket index — how finely the console quantizes it into
// buckets is hardware, not contract.
struct rv_primitive {
    uint32_t type;  // rv_primitive_type: selects the union member
    int32_t depth;  // ordering-table sort key (larger = nearer / on top)

    union {
        rv_line line;
        rv_polygon polygon;
        rv_sprite sprite;
    } data;
};

}  // namespace rv_3dmppc
