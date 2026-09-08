#ifndef RV_PDK_CV_RV_VERTEX_H
#define RV_PDK_CV_RV_VERTEX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

struct rv_color {
    uint8_t r, g, b;
};

struct rv_uv {
    uint16_t u, v;
};

struct rv_vertex {
    int16_t x, y;
    struct rv_color color;
    struct rv_uv uv;
};

/* Plain names: without a typedef, C demands `struct rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as an
   ordinary type. */
typedef struct rv_color rv_color;
typedef struct rv_uv rv_uv;
typedef struct rv_vertex rv_vertex;

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CV_RV_VERTEX_H
