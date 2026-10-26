// The software geometry controller: the rv_cg contract over a model memory it owns
// and a camera it remembers. A model is kept as its decoded integer model, charged by its
// .mppcmod size against model_memory_size. Not an rv_pcpool: a pool
// hands out bytes that a model would have to be serialized into, and nothing here
// is ever read back from bytes.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "pdk/cg/rv_cg.h"
#include "pdklib/rv_gte/rv_gte_camera.hpp"
#include "pdklib/rv_models/rv_mppcmod_int.hpp"
#include "rv_pconsole/cg/rv_pccg.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

// The integer point of rv_pccg_sw::point: view by rv_gte_point, screen by rv_gte_project, depth by
// rv_gte_depth_key. Free, so a probe runs it without the console. Error: RV_ERR_INVAL for a point nearer
// than the near plane or a view value that leaves int32; screen and depth are then untouched.
// frame_camera refuses a camera whose three times (far minus near) is above gte_key_span_max, the triangle
// key, so no point of such a frame is refused for its span.
int rv_pccg_sw_point_int(const rv_pdklib::rv_gte_camera &camera,
    rv_pdklib::rv_gte_viewport viewport,
    rv_pdklib::fx_vec3 world,
    int32_t depth_min,
    int32_t depth_max,
    rv_pdklib::rv_gte_screen &screen,
    int32_t &depth);

// A screen side as int32. Error: the side is outside (0, INT32_MAX]; out is untouched.
int rv_pccg_sw_screen_side(int64_t side, int32_t &out);

// Corners of a triangle, and of a triangle cut at the near plane.
constexpr int RV_PCCG_SW_TRI_CORNERS = 3;
constexpr int RV_PCCG_SW_CUT_CORNERS = 4;

// A vertex in view space. colour (0..255 per channel, before the tint) and uv (4.12) are interpolated
// with the position at the near cut.
struct rv_pccg_sw_clip_vertex {
    rv_pdklib::fx_vec3 view;
    std::array<int32_t, 3> colour;
    std::array<int32_t, 2> uv;
};

// A model vertex in view space. A vertex outside int32 is not valid: its triangles are skipped.
struct rv_pccg_sw_view_vertex {
    rv_pdklib::fx_vec3 view;
    bool valid;
};

// View-space Q20.12 of every model vertex: rv_gte_compose(camera view, world), then rv_gte_vertex.
// Error: the composed matrix leaves rv_gte's range. A vertex that rv_gte_vertex refuses is marked, not an error.
int rv_pccg_sw_view_vertexes(const rv_pdklib::rv_gte_matrix &camera_view,
    const rv_pdklib::rv_gte_matrix &world,
    const rv_pdklib::rv_mppcmod_int_model &model,
    std::vector<rv_pccg_sw_view_vertex> &out);

// Sutherland-Hodgman against z == near_plane; a vertex is in when z >= near_plane, the side
// rv_gte_near_clip cuts to. Returns 0, 3 or 4 vertices in `out`, through `count`.
int rv_pccg_sw_clip_near(const rv_pccg_sw_clip_vertex (&in)[RV_PCCG_SW_TRI_CORNERS],
    rv_pdklib::fx near_plane,
    rv_pccg_sw_clip_vertex (&out)[RV_PCCG_SW_CUT_CORNERS],
    int &count);

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
    int64_t point(const rv_cg_vec3 *world, int32_t depth_min, int32_t depth_max, rv_cg_screen *out) override;

    // --- console-side ---

    // Model memory is a bookkeeping budget, not reserved pages. The record array is
    // reserved by the constructor, so valid() is true: a failed reserve throws and the
    // console is never built.
    bool valid() const override
    {
        return true;
    }

    void frame_reset() override;
    int64_t model_upload(const rv_pdklib::rv_mppcmod_int_model &model, std::span<const rv_pccg_part_texture> textures) override;
    int64_t model_textures_set(int64_t addr, std::span<const rv_pccg_part_texture> textures) override;
    int64_t model_free(int64_t addr) override;

private:
    struct model_record {
        int64_t addr;
        int64_t bytes;
        rv_pdklib::rv_mppcmod_int_model int_model;
        std::vector<rv_pccg_part_texture> textures;
    };

    std::vector<model_record>::iterator find(int64_t addr);

    rv_pccg_conf conf_;
    // the frame of cv the geometry puts its primitives into
    rv_pccv &cv_;

    std::vector<model_record> models_;
    int64_t used_bytes_ = 0;
    int64_t next_addr_;

    rv_pdklib::rv_gte_camera gte_camera_ = {};
    bool camera_set_ = false;
    int64_t triangles_ = 0;
};

} // namespace rv_3dmppc
