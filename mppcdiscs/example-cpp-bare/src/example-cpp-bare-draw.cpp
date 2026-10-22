#include "example-cpp-bare.hpp"

#include <cstddef>
#include <iterator>

#include "pdk/cd/rv_cd.h"
#include "pdk/cg/rv_cg.h"
#include "pdk/cv/rv_primitives.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"

namespace example_cpp_bare
{

namespace
{

// The project's decal bias, the same as Solid Maid's: a decal sorts this many keys nearer than the world it lies on.
constexpr int32_t RV_DRAW_DECAL_BIAS = 260;
// The world takes the lower part of the ordering table; the decals take the same width, shifted nearer.
constexpr int32_t RV_DRAW_WORLD_MIN = RV_DEPTH_MIN;
constexpr int32_t RV_DRAW_WORLD_MAX = RV_DEPTH_MAX - RV_DRAW_DECAL_BIAS;
constexpr int32_t RV_DRAW_DECAL_MIN = RV_DEPTH_MIN + RV_DRAW_DECAL_BIAS;
constexpr int32_t RV_DRAW_DECAL_MAX = RV_DEPTH_MAX;
// No entity of the scene is two-sided, so every model is culled at its back faces.
constexpr uint32_t RV_DRAW_CULL = RV_CG_CULL_BACK;

// Copies the world matrix field by field: rv_cg_matrix has the same units, and no layout is assumed between the two types.
void copy_matrix(const rv_pdklib::rv_gte_matrix &from, rv_cg_matrix &to)
{
    for (std::size_t i = 0; i < std::size(to.r); ++i) {
        to.r[i] = from.r[i];
    }
    for (std::size_t i = 0; i < std::size(to.t); ++i) {
        to.t[i] = from.t[i];
    }
}

} // namespace

int rv_draw_camera(rv_pdko *pdk, const rv_cg_camera &camera)
{
    const int64_t camera_code = rv_cg_frame_camera(rv_pdko_cg(pdk), &camera);
    const bool failed = camera_code < 0;
    return failed ? static_cast<int>(camera_code) : RV_OK;
}

int rv_draw_put(rv_pdko *pdk, const char *model, const rv_pdklib::rv_gte_matrix &world, rv_color tint, bool decal)
{
    const int64_t addr = rv_cd_resource_addr(rv_pdko_cd(pdk), RV_CD_RESOURCE_MODEL, model);
    if (addr < 0) {
        return static_cast<int>(addr);
    }
    rv_cg_put put = {};
    copy_matrix(world, put.world);
    put.depth_min = decal ? RV_DRAW_DECAL_MIN : RV_DRAW_WORLD_MIN;
    put.depth_max = decal ? RV_DRAW_DECAL_MAX : RV_DRAW_WORLD_MAX;
    put.cull = RV_DRAW_CULL;
    put.tint = tint;
    const int64_t count = rv_cg_model_put(rv_pdko_cg(pdk), addr, &put);
    const bool failed = count < 0;
    return failed ? static_cast<int>(count) : RV_OK;
}

} // namespace example_cpp_bare
