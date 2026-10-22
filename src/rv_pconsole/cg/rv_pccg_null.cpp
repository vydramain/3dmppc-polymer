#include "rv_pconsole/cg/rv_pccg_null.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

namespace
{
// Same reasoning as rv_pccv_null: nothing is allocated, so repeats are fine and
// no pool can run out. Positive, so a disc never mistakes it for an error.
constexpr int64_t RV_PCCG_NULL_FAKE_ADDR = 16;
} // namespace

int rv_pccg_null::evaluate(const rv_pdklib::rv_manifest_budget & /*budget*/, int64_t &bytes, std::string & /*error*/)
{
    bytes = 0;
    return RV_OK;
}

rv_pccg_null::rv_pccg_null(const rv_pccg_conf &conf)
    : conf_(conf)
{
    RV_LOG_INFO("pccg", "geometry off ({} triangles per frame declared, no projection, no-op)", conf_.triangle_capacity);
}

// --- budget ------------------------------------------------------------------

int64_t rv_pccg_null::model_memory_size()
{
    return conf_.model_memory_size;
}

int64_t rv_pccg_null::triangle_capacity()
{
    return conf_.triangle_capacity;
}

// --- the frame ---------------------------------------------------------------

int64_t rv_pccg_null::frame_camera(const rv_cg_camera *camera)
{
    const bool absent = camera == nullptr;
    if (absent) {
        return RV_ERR_INVAL;
    }
    camera_set_ = true;
    return RV_OK;
}

int64_t rv_pccg_null::model_put(int64_t /*model_addr*/, const rv_cg_put *put)
{
    // the argument refusals of the real controller; triangles are not counted, so never RV_ERR_NOMEM
    const bool no_camera = !camera_set_;
    if (no_camera) {
        return RV_ERR_INVAL;
    }
    const bool no_put = put == nullptr;
    if (no_put) {
        return RV_ERR_INVAL;
    }
    const bool cull_known = put->cull == RV_CG_CULL_NONE || put->cull == RV_CG_CULL_BACK;
    if (!cull_known) {
        return RV_ERR_INVAL;
    }
    const bool depth_ordered = put->depth_min <= put->depth_max;
    if (!depth_ordered) {
        return RV_ERR_INVAL;
    }
    // Nothing transformed, so zero primitives reach cv.
    return 0;
}

int64_t rv_pccg_null::point(const rv_cg_vec3 * /*world*/, int32_t /*depth_min*/, int32_t /*depth_max*/, rv_cg_screen * /*out*/)
{
    // no projection engine: always RV_ERR_INVAL, with or without a camera
    return RV_ERR_INVAL;
}

// --- console-side ------------------------------------------------------------

void rv_pccg_null::frame_reset()
{
    camera_set_ = false;
}

int64_t rv_pccg_null::model_upload(const rv_pdklib::rv_mppcmod_int_model &model, std::span<const rv_pccg_part_texture> textures)
{
    // Argument validation stays on for null: a disc bug must look the same with cg off.
    if (textures.size() != model.parts.size()) {
        return RV_ERR_INVAL;
    }
    return RV_PCCG_NULL_FAKE_ADDR;
}

int64_t rv_pccg_null::model_textures_set(int64_t addr, std::span<const rv_pccg_part_texture> /*textures*/)
{
    // Known gap: no model is kept, so the texture count cannot be checked against its parts.
    // Only the address is checked: the one model_upload hands out is the only one accepted.
    const bool unknown = addr != RV_PCCG_NULL_FAKE_ADDR;
    if (unknown) {
        RV_LOG_WARN("pccg", "textures refused: no model at address {}", addr);
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

int64_t rv_pccg_null::model_free(int64_t /*addr*/)
{
    return RV_OK;
}

} // namespace rv_3dmppc
