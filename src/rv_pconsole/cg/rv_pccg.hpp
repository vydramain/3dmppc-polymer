// The rv_cg contract made abstract, the same shape as rv_pccv: argument handling and
// addresses in and out. What "geometry" means underneath is the concrete class's
// choice (rv_pccg_null: nothing is transformed).
#pragma once

#include <cstdint>
#include <span>

#include "pdk/cg/rv_cg.h"
#include "pdklib/rv_models/rv_mppcmod.hpp"

namespace rv_3dmppc
{

// A resident texture of one model part. addr_texture = 0 for a part with no texture;
// width and height are the texel size, needed to map the model's 0..1 uv to texels.
struct rv_pccg_part_texture {
    int64_t addr_texture;
    int64_t addr_palette;
    int64_t width;
    int64_t height;
};

class rv_pccg
{
public:
    virtual ~rv_pccg() = default;

    rv_pccg(const rv_pccg &) = delete;
    rv_pccg &operator=(const rv_pccg &) = delete;

    // --- budget: straight out of the configuration ---

    virtual int64_t model_memory_size() = 0;
    virtual int64_t triangle_capacity() = 0;

    // --- the frame ---

    virtual int64_t frame_camera(const rv_cg_camera *camera) = 0;
    virtual int64_t model_put(int64_t model_addr, const rv_cg_put *put) = 0;
    virtual int64_t point(const rv_cg_vec3 *world, rv_cg_screen *out) = 0;

    // --- console-side: never reached through the extern "C" block ---

    // Does the memory this controller owns actually exist?
    virtual bool valid() const = 0;

    // Start of a frame: forget the camera and the triangles counted so far.
    virtual void frame_reset() = 0;

    // Address in the model memory, or RV_ERR_NOMEM / RV_ERR_INVAL (one texture per part).
    // The texture addresses and sizes are copied at upload: a texture rewritten at the
    // same address (video_asset_write) stays valid; a new texture address needs
    // model_free + model_upload.
    virtual int64_t model_upload(const rv_pdklib::rv_mppcmod &model, std::span<const rv_pccg_part_texture> textures) = 0;
    virtual int64_t model_free(int64_t addr) = 0;

protected:
    rv_pccg() = default;
};

} // namespace rv_3dmppc
