// The software geometry controller: the rv_cg contract over a model memory it owns
// and a camera it remembers. A model is kept as its decoded rv_mppcmod copy and
// charged by its .mppcmod size against model_memory_size. Not an rv_pcpool: a pool
// hands out bytes that a model would have to be serialized into, and nothing here
// is ever read back from bytes.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "pdk/cg/rv_cg.h"
#include "pdklib/rv_models/rv_mppcmod.hpp"
#include "rv_pconsole/cg/rv_pccg.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_pdklib
{
// Only named here: the .cpp files that build or use them include the full headers.
struct rv_mat4;
struct rv_xform_conf;
} // namespace rv_pdklib

namespace rv_3dmppc
{

class rv_pccg_sw final : public rv_pccg
{
public:
    rv_pccg_sw(const rv_pccg_conf &conf, rv_pccv &cv);

    // Upper bound on the host bytes the decoded models take while model_memory_size
    // bytes of .mppcmod are resident, plus one record per model the smallest model
    // allows. Overflow fails with RV_ERR_INVAL and `error` set.
    static int evaluate(const rv_pdklib::rv_manifest_budget &budget, int64_t &bytes, std::string &error);

    // --- budget: straight out of the configuration ---

    int64_t model_memory_size() override;
    int64_t triangle_capacity() override;

    // --- the frame ---

    int64_t frame_camera(const rv_cg_camera *camera) override;
    int64_t model_put(int64_t model_addr, const rv_cg_put *put) override;
    int64_t point(const rv_cg_vec3 *world, rv_cg_screen *out) override;

    // --- console-side ---

    // Model memory is a bookkeeping budget, not reserved pages. The record array is
    // reserved by the constructor, so valid() is true: a failed reserve throws and the
    // console is never built.
    bool valid() const override
    {
        return true;
    }

    void frame_reset() override;
    int64_t model_upload(const rv_pdklib::rv_mppcmod &model, std::span<const rv_pccg_part_texture> textures) override;
    int64_t model_free(int64_t addr) override;

private:
    struct model_record {
        int64_t addr;
        int64_t bytes;
        rv_pdklib::rv_mppcmod model;
        std::vector<rv_pccg_part_texture> textures;
    };

    std::vector<model_record>::iterator find(int64_t addr);
    // The frame's xform conf: camera from camera_, screen size from cv_.
    rv_pdklib::rv_xform_conf frame_conf(const rv_pdklib::rv_mat4 &world, int32_t depth_min, int32_t depth_max, uint32_t cull);

    rv_pccg_conf conf_;
    // the frame of cv the geometry puts its primitives into
    rv_pccv &cv_;

    std::vector<model_record> models_;
    int64_t used_bytes_ = 0;
    int64_t next_addr_;

    rv_cg_camera camera_ = {};
    bool camera_set_ = false;
    int64_t triangles_ = 0;
};

} // namespace rv_3dmppc
