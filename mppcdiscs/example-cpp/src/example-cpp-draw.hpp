#pragma once

#include <cstdint>
#include <cstring>

#include "pdk/cd/rv_cd.h"
#include "pdk/cg/rv_cg.h"
#include "pdk/cv/rv_primitives.h"
#include "pdk/cv/rv_vertex.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"

namespace example_cpp
{

// The decal window is the world window shifted RV_DRAW_DECAL_BIAS keys nearer, same width
// (pdk/include/pdk/cv/rv_primitives.h), so a decal wins over the wall it lies on. Same bias as
// Solid Maid's SM_DEPTH_BIAS_DECAL (solid-maid/src/sm_common.hpp).
inline constexpr int32_t RV_DRAW_DECAL_BIAS = 260;
inline constexpr int32_t RV_DRAW_DEPTH_WORLD_MIN = RV_DEPTH_MIN;
inline constexpr int32_t RV_DRAW_DEPTH_WORLD_MAX = RV_DEPTH_MAX - RV_DRAW_DECAL_BIAS;
inline constexpr int32_t RV_DRAW_DEPTH_DECAL_MIN = RV_DEPTH_MIN + RV_DRAW_DECAL_BIAS;
inline constexpr int32_t RV_DRAW_DEPTH_DECAL_MAX = RV_DEPTH_MAX;

// Models are not tinted: SAMPLE_TEXTURE replaces the vertex colour with the texel.
inline constexpr rv_color RV_DRAW_TINT = { 255, 255, 255 };

// Puts the model name (a .mppcmod on the disc) at the row-major world matrix, culled back, in the depth window.
// Returns RV_OK or the error of the drive or cg.
inline int64_t rv_draw_model_window(rv_pdko *pdk, const char *name, const float *world, int32_t depth_min, int32_t depth_max)
{
    const int64_t addr = rv_cd_resource_addr(rv_pdko_cd(pdk), RV_CD_RESOURCE_MODEL, name);
    if (addr < 0) {
        return addr;
    }
    rv_cg_put put = {};
    std::memcpy(put.world, world, sizeof(put.world));
    put.depth_min = depth_min;
    put.depth_max = depth_max;
    put.cull = RV_CG_CULL_BACK;
    put.tint = RV_DRAW_TINT;
    const int64_t count = rv_cg_model_put(rv_pdko_cg(pdk), addr, &put);
    if (count < 0) {
        return count;
    }
    return RV_OK;
}

// A world model (the lamps, the scene meshes).
inline int64_t rv_draw_model(rv_pdko *pdk, const char *name, const float *world)
{
    return rv_draw_model_window(pdk, name, world, RV_DRAW_DEPTH_WORLD_MIN, RV_DRAW_DEPTH_WORLD_MAX);
}

// A decal on a wall (the board halos and letters).
inline int64_t rv_draw_decal(rv_pdko *pdk, const char *name, const float *world)
{
    return rv_draw_model_window(pdk, name, world, RV_DRAW_DEPTH_DECAL_MIN, RV_DRAW_DEPTH_DECAL_MAX);
}

} // namespace example_cpp
