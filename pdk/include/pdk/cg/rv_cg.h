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

// Position in meters, Q20.12: 4096 is one metre.
struct rv_cg_vec3 {
    int32_t x, y, z;
};

typedef struct rv_cg_vec3 rv_cg_vec3;

// Affine transform: a point p maps to r * p + t. r is the rotation times the scale in 4.12 (4096 is 1.0),
// row-major, r[row * 3 + col]; t is the translation in Q20.12. Same layout as pdklib's rv_gte_matrix.
struct rv_cg_matrix {
    int16_t r[9];
    int32_t t[3];
};

typedef struct rv_cg_matrix rv_cg_matrix;

// Same fields as pdklib's rv_gte_camera. view maps world to view, left-handed: its rows are right, up and
// forward, and +z is in front of the eye. h is the focal length in pixels, (screen height / 2) / tan(fov_y / 2).
// near_plane and far_plane are view z in Q20.12. view is taken as is; pdklib's rv_gte_camera_make builds it
// from eye, target, up and the fov in degrees.
struct rv_cg_camera {
    rv_cg_matrix view;
    int32_t h;
    int32_t near_plane, far_plane;
};

typedef struct rv_cg_camera rv_cg_camera;

// world maps object to world, as rv_cg_matrix. depth_min..depth_max is the ordering-table key range the
// disc picks for this model, so the disc decides where it sorts against its other layers.
struct rv_cg_put {
    rv_cg_matrix world;
    int32_t depth_min, depth_max;
    uint32_t cull;
    struct rv_color tint;
};

typedef struct rv_cg_put rv_cg_put;

// Screen point in pixels, y down. depth is the ordering-table key a primitive at this point sorts by.
struct rv_cg_screen {
    int32_t x, y;
    int32_t depth;
};

typedef struct rv_cg_screen rv_cg_screen;

// Budget queries: bytes of model memory, and triangles one frame may hold.
int64_t rv_cg_model_memory_size(rv_cg *cg);
int64_t rv_cg_triangle_capacity(rv_cg *cg);

// Camera for the puts that follow in this frame. A null camera, planes not 0 < near_plane < far_plane, a
// far_plane - near_plane span that three triangle keys cannot hold, h below 1, or a screen height not in
// 1..INT32_MAX answers RV_ERR_INVAL and keeps the camera set earlier in this frame, if any.
int64_t rv_cg_frame_camera(rv_cg *cg, const rv_cg_camera *camera);

// model_addr comes from rv_cd_resource_addr(cd, RV_CD_RESOURCE_MODEL, name) and
// is valid until that model is reloaded. Transforms the model by put->world, then by the camera's view,
// cuts triangles at the near plane (split, not dropped), culls per put->cull, keys them into
// [depth_min, depth_max] and puts the primitives into the frame of cv. Returns the number of primitives put.
// RV_ERR_INVAL, nothing put: no camera this frame, a null put, a cull that is not a known mode,
// depth_min > depth_max, an unknown model_addr, a textured part with no size, a composed matrix that leaves
// rv_gte's range, or a triangle whose projection or depth key rv_gte refuses.
// RV_ERR_NOMEM, nothing put: the primitives would pass the frame's triangle_capacity. A model put half-way
// would show as a hole, not as a smaller model. A refusal by cv part-way through leaves the primitives already
// put and returns cv's error. A triangle with a vertex outside int32, or with a uv span over 65535 texels,
// is skipped and the rest of the model is put.
int64_t rv_cg_model_put(rv_cg *cg, int64_t model_addr, const rv_cg_put *put);

// Projects a world point with the frame's camera. depth is keyed into [depth_min, depth_max], the window
// of the put that it sorts with. RV_ERR_INVAL, nothing written to out: no camera this frame, a null argument,
// depth_min > depth_max, a point with view z below near_plane (it has no usable screen position), or a value
// rv_gte refuses.
int64_t rv_cg_point(rv_cg *cg, const rv_cg_vec3 *world, int32_t depth_min, int32_t depth_max, rv_cg_screen *out);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CG_RV_CG_H
