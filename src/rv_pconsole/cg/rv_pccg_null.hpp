// The geometry controller with no geometry engine: budget answers from the
// configuration, puts are checked and then dropped. What cg=null means - the rv_cg
// slot's Null Object, as rv_pccv_null is for cv.
#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "pdk/cg/rv_cg.h"
#include "pdklib/rv_models/rv_mppcmod_int.hpp"
#include "rv_pconsole/cg/rv_pccg.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

class rv_pccg_null final : public rv_pccg
{
public:
    explicit rv_pccg_null(const rv_pccg_conf &conf);

    // No engine, no pool: RV_OK, bytes = 0.
    static int evaluate(const rv_pdklib::rv_manifest_budget &budget, int64_t &bytes, std::string &error);

    // --- budget: straight out of the configuration ---

    int64_t model_memory_size() override;
    int64_t triangle_capacity() override;

    // --- the frame: the camera is remembered, nothing is transformed or put ---

    int64_t frame_camera(const rv_cg_camera *camera) override;
    int64_t model_put(int64_t model_addr, const rv_cg_put *put) override;
    int64_t point(const rv_cg_vec3 *world, int32_t depth_min, int32_t depth_max, rv_cg_screen *out) override;

    // --- console-side ---

    bool valid() const override
    {
        return true;
    }

    void frame_reset() override;
    int64_t model_upload(const rv_pdklib::rv_mppcmod_int_model &model, std::span<const rv_pccg_part_texture> textures) override;
    int64_t model_textures_set(int64_t addr, std::span<const rv_pccg_part_texture> textures) override;
    int64_t model_free(int64_t addr) override;

private:
    rv_pccg_conf conf_;
    bool camera_set_ = false;
};

} // namespace rv_3dmppc
