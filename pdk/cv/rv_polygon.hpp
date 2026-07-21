#pragma once

#include <cstdint>

#include "rv_vertex.hpp"

namespace rv_3dmppc {

// What happens when a texture coordinate falls outside the texture. This is a
// per-polygon DRAWING attribute (the same uploaded texture may be clamped on one
// polygon and tiled on another), not a property of the stored texels.
//
// New modes take a new enumerator; existing values are ABI.
enum rv_texture_mapping_type {
    RV_TEXWRAP_CLAMP = 1,    // clamp to the edge texel
    RV_TEXWRAP_TILE = 2,     // repeat
    RV_TEXWRAP_STRETCH = 3,  // stretch the texture across the polygon
};

// A triangle handed to rv_cv::frame_put() inside an rv_primitive. Self-contained:
// it carries its own geometry AND every attribute needed to draw it, because the
// console re-orders primitives by depth (see rv_primitive) — there is no global
// "current texture / current state" a polygon could rely on.
//
// The texture and palette are referenced by the addresses rv_cv::video_asset_malloc
// returned; addr_palette matters only for the indexed rv_texfmt formats.
//
// DEFERRED: shading (flat/gouraud), blending (opaque/semi-transparent) and texture
// combine (raw/modulation) flags.
struct rv_polygon {
    uint32_t textured;  // 0 = flat-coloured; non-zero = sample the texture below

    int64_t addr_texture;             // texture data in video RAM (if textured)
    int64_t addr_palette;             // palette in video RAM (indexed formats)
    rv_texture_mapping_type mapping;  // how to sample outside the texture

    rv_vertex vertexes[3];
};

}  // namespace rv_3dmppc
