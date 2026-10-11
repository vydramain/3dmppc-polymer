#ifndef RV_PDK_CG_RV_CG_H
#define RV_PDK_CG_RV_CG_H

#include <stdint.h>

#include "pdk/cv/rv_vertex.h" // rv_color: put tint

// Plain values, not a bit mask: one mode per put.
#define RV_CG_CULL_NONE 0
#define RV_CG_CULL_BACK 1

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

// The geometry controller (GTE on the PSX): it transforms models and puts
// their triangles into the frame of cv. cv itself does not change.
typedef struct rv_cg rv_cg;

struct rv_cg_vec3 {
    float x, y, z;
};

typedef struct rv_cg_vec3 rv_cg_vec3;

// fov_y is in radians. The screen is the frame size cv reports.
struct rv_cg_camera {
    rv_cg_vec3 eye, target, up;
    float fov_y;
    float near_plane, far_plane;
};

typedef struct rv_cg_camera rv_cg_camera;

// world is row-major: element (row, col) is world[row*4+col], and points are
// column vectors multiplied from the right (the layout of pdklib's rv_mat4).
// depth_min..depth_max is the ordering-table key range the disc picks for
// this model, so the disc decides where it sorts against its other layers.
struct rv_cg_put {
    float world[16];
    int32_t depth_min, depth_max;
    uint32_t cull;
    struct rv_color tint;
};

typedef struct rv_cg_put rv_cg_put;

// depth is the ordering-table key a primitive at this point sorts by.
struct rv_cg_screen {
    float x, y;
    int32_t depth;
};

typedef struct rv_cg_screen rv_cg_screen;

// Budget queries: bytes of model memory, and triangles one frame may hold.
int64_t rv_cg_model_memory_size(rv_cg *cg);
int64_t rv_cg_triangle_capacity(rv_cg *cg);

// Camera for the puts that follow in this frame.
int64_t rv_cg_frame_camera(rv_cg *cg, const rv_cg_camera *camera);

// model_addr comes from rv_cd_resource_addr(cd, RV_CD_RESOURCE_MODEL, name) and
// is valid until that model is reloaded. Transforms the model by put->world,
// cuts triangles at the near plane (split, not dropped), culls per put->cull,
// keys them into [depth_min, depth_max] and puts the primitives into the frame
// of cv. Returns the number of primitives put. No camera this frame, an unknown
// cull, or depth_min > depth_max answers RV_ERR_INVAL and puts nothing.
// Going over triangle_capacity answers RV_ERR_NOMEM and puts nothing: a model
// put half-way would show as a hole, not as a smaller model. A refusal by cv
// part-way through leaves the primitives already put and returns cv's error.
// A textured part with no size answers RV_ERR_INVAL and puts nothing; a triangle
// whose uv spans more than 65535 texels is skipped and the rest of the model is put.
int64_t rv_cg_model_put(rv_cg *cg, int64_t model_addr, const rv_cg_put *put);

// Projects a world point with the frame's camera; no camera this frame answers
// RV_ERR_INVAL, as put does. A point nearer than the near plane has no usable
// screen position (the divide by w flips or blows up), so it also answers
// RV_ERR_INVAL and writes nothing to out.
int64_t rv_cg_point(rv_cg *cg, const rv_cg_vec3 *world, rv_cg_screen *out);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CG_RV_CG_H
