#include <array>
#include <cstdint>
#include <vector>

#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_gte/rv_gte_project.hpp"
#include "rv_pconsole/cg/rv_pccg_sw.hpp"

namespace rv_3dmppc
{

namespace
{

// Interpolates each attribute at the cut; t is the fraction from below to above.
template <size_t N>
void lerp_attributes(const std::array<int32_t, N> &below,
    const std::array<int32_t, N> &above,
    int64_t t,
    std::array<int32_t, N> &cut)
{
    for (size_t k = 0; k < N; ++k) {
        cut[k] = static_cast<int32_t>(rv_pdklib::rv_gte_lerp(below[k], above[k], t));
    }
}

} // namespace

int rv_pccg_sw_view_vertexes(const rv_pdklib::rv_gte_matrix &camera_view,
    const rv_pdklib::rv_gte_matrix &world,
    const rv_pdklib::rv_mppcmod_int_model &model,
    std::vector<rv_pccg_sw_view_vertex> &out)
{
    rv_pdklib::rv_gte_matrix mv = {};
    const int compose_rc = rv_pdklib::rv_gte_compose(camera_view, world, mv);
    if (compose_rc != RV_OK) {
        return compose_rc;
    }
    out.resize(model.vertices.size());
    for (size_t i = 0; i < model.vertices.size(); ++i) {
        int32_t v[3] = {};
        const int vertex_rc = rv_pdklib::rv_gte_vertex(mv, model.vertices[i].pos.data(), model.shift, v);
        out[i] = { { v[0], v[1], v[2] }, vertex_rc == RV_OK };
    }
    return RV_OK;
}

int rv_pccg_sw_clip_near(const rv_pccg_sw_clip_vertex (&in)[RV_PCCG_SW_TRI_CORNERS],
    rv_pdklib::fx near_plane,
    rv_pccg_sw_clip_vertex (&out)[RV_PCCG_SW_CUT_CORNERS],
    int &count)
{
    count = 0;
    for (int i = 0; i < RV_PCCG_SW_TRI_CORNERS; ++i) {
        const rv_pccg_sw_clip_vertex &a = in[i];
        const rv_pccg_sw_clip_vertex &b = in[(i + 1) % RV_PCCG_SW_TRI_CORNERS];
        const bool a_in = a.view.z >= near_plane;
        const bool b_in = b.view.z >= near_plane;
        if (a_in) {
            out[count++] = a;
        }
        if (a_in != b_in) {
            // The cut point of the edge below -> above. Position and attributes take the same fraction t.
            const rv_pccg_sw_clip_vertex &below = a_in ? b : a;
            const rv_pccg_sw_clip_vertex &above = a_in ? a : b;
            rv_pccg_sw_clip_vertex &cut = out[count];
            const int rc = rv_pdklib::rv_gte_near_clip(below.view, above.view, near_plane, cut.view);
            if (rc != RV_OK) {
                return rc;
            }
            const int64_t t = rv_pdklib::rv_gte_near_t(below.view.z, above.view.z, near_plane);
            lerp_attributes(below.colour, above.colour, t, cut.colour);
            lerp_attributes(below.uv, above.uv, t, cut.uv);
            ++count;
        }
    }
    return RV_OK;
}

} // namespace rv_3dmppc
