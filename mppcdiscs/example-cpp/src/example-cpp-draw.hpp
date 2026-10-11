#pragma once

#include <cstdint>
#include <cstring>

#include "pdk/cd/rv_cd.h"
#include "pdk/cg/rv_cg.h"
#include "pdk/cv/rv_vertex.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"

namespace example_cpp
{

// The ordering-table window of the world: a larger key is nearer and drawn over a smaller one.
// cv divides its own key window into buckets (pdk/include/pdk/cv/rv_primitives.h), so these are relative.
inline constexpr int32_t RV_DRAW_DEPTH_WORLD_MIN = 0;
inline constexpr int32_t RV_DRAW_DEPTH_WORLD_MAX = 959;

// Models are not tinted: SAMPLE_TEXTURE replaces the vertex colour with the texel.
inline constexpr rv_color RV_DRAW_TINT = { 255, 255, 255 };

// Puts the model name (a .mppcmod on the disc) at the row-major world matrix, culled back, in the world window.
// Returns RV_OK or the error of the drive or cg.
inline int64_t rv_draw_model(rv_pdko *pdk, const char *name, const float *world)
{
    const int64_t addr = rv_cd_resource_addr(rv_pdko_cd(pdk), RV_CD_RESOURCE_MODEL, name);
    if (addr < 0) {
        return addr;
    }
    rv_cg_put put = {};
    std::memcpy(put.world, world, sizeof(put.world));
    put.depth_min = RV_DRAW_DEPTH_WORLD_MIN;
    put.depth_max = RV_DRAW_DEPTH_WORLD_MAX;
    put.cull = RV_CG_CULL_BACK;
    put.tint = RV_DRAW_TINT;
    const int64_t count = rv_cg_model_put(rv_pdko_cg(pdk), addr, &put);
    if (count < 0) {
        return count;
    }
    return RV_OK;
}

} // namespace example_cpp
