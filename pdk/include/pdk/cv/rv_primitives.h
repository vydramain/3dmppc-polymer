#ifndef RV_PDK_CV_RV_PRIMITIVES_H
#define RV_PDK_CV_RV_PRIMITIVES_H

#include <stdint.h>

#include "pdk/cv/rv_vertex.h"

// Values for the `fill_mode` and `type` fields. These are NOT masks: a field
// holds exactly one of them, which is why they are numbered consecutively
// instead of by bit.
#define RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED  UINT32_C(1)
#define RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE UINT32_C(2)
#define RV_PRIMITIVE_FILL_MODE_WIREFRAME      UINT32_C(3)

#define RV_PRIMITIVE_LINE    UINT32_C(1) // rv_primitive.data.line
#define RV_PRIMITIVE_POLYGON UINT32_C(2) // rv_primitive.data.polygon
#define RV_PRIMITIVE_SPRITE  UINT32_C(3) // rv_primitive.data.sprite

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

struct rv_line {
    struct rv_vertex vertexes[2];
};

enum rv_texture_mapping_type {
    RV_TEXWRAP_CLAMP = 1,   // clamp to the edge texel
    RV_TEXWRAP_TILE = 2,    // repeat
    RV_TEXWRAP_STRETCH = 3, // stretch the texture across the primitive
};

struct rv_polygon {
    uint32_t fill_mode; // one of RV_PRIMITIVE_FILL_MODE_*

    int64_t addr_texture;                 // texture data in video RAM (if sampling)
    int64_t addr_palette;                 // palette in video RAM (indexed formats)
    enum rv_texture_mapping_type mapping; // how to sample outside the texture

    uint32_t vertex_count;        // 3 = triangle, 4 = quad; anything else = RV_ERR_INVAL
    struct rv_vertex vertexes[4]; // vertexes[3] is ignored when vertex_count == 3
};

struct rv_sprite {
    uint32_t fill_mode; // one of RV_PRIMITIVE_FILL_MODE_*

    int64_t addr_texture; // texture data in video RAM (if sampling)
    int64_t addr_palette; // palette in video RAM (indexed formats)

    struct rv_color color;
    enum rv_texture_mapping_type mapping; // how to sample outside the texture

    int16_t x, y;           // upper-left corner (signed: may start off-screen)
    uint16_t width, height; // extent in pixels
};

struct rv_primitive {
    uint32_t type; // one of RV_PRIMITIVE_*: selects the union member
    int32_t depth; // ordering-table sort key (larger = nearer / on top)

    union {
        struct rv_line line;
        struct rv_polygon polygon;
        struct rv_sprite sprite;
    } data;
};

/* Plain names: without a typedef, C demands `struct rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as an
   ordinary type. */
typedef struct rv_line rv_line;
typedef enum rv_texture_mapping_type rv_texture_mapping_type;
typedef struct rv_polygon rv_polygon;
typedef struct rv_sprite rv_sprite;
typedef struct rv_primitive rv_primitive;

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CV_RV_PRIMITIVES_H
