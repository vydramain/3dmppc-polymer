#pragma once

#include <cstdint>

namespace rv_3dmppc {

// A 24-bit RGB colour: vertex colours (shading / modulation), flat fills, and
// the frame clear colour. NOT a palette entry — palette entries are 16-bit texel
// values, see rv_texture.hpp.
struct rv_color {
    uint8_t r, g, b;
};

// Texture coordinates for one vertex, in texels (origin at the texture's
// upper-left). Pure data with no error channel, so unsigned. What happens for
// coordinates outside the texture is chosen per polygon — see
// rv_texture_mapping_type; it is not a wrap baked into the coordinate.
struct rv_uv {
    uint16_t u, v;
};

// One corner of a primitive: a screen position, a colour, and a texture
// coordinate. x/y are signed because a vertex may sit off-screen (clipped). The
// colour is used for shading; the uv is read only when the primitive's
// fill_mode samples a texture.
struct rv_vertex {
    int16_t x, y;
    rv_color color;
    rv_uv uv;
};

}  // namespace rv_3dmppc
