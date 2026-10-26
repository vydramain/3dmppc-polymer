// The integer near-plane cut of the Scene viewport's triangles. It mirrors the console's cut in
// src/rv_pconsole/cg/rv_pccg_sw_view.cpp (rv_pccg_sw_clip_near): the same edge walk, the same
// rv_gte_near_clip and rv_gte_near_t, and the same fan. The editor cannot include console sources
// (a separate target), so it keeps this copy of the arithmetic; change both together.

#include "panes/rv_editor_scene_draw.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_gte/rv_gte_project.hpp"

namespace rv_editor
{

namespace
{

// The cut polygon has at most four corners: the two kept corners plus the two cut points of the two crossed
// edges. Three in front has no cut.
constexpr int max_corners = 4;

float lerp_f(float a, float b, float f)
{
    return a + (b - a) * f;
}

// The cut point of the edge below -> above, on near_plane. Position, uv and colour take the same t.
int rv_editor_fx_cut(const rv_editor_fx_vertex &below,
    const rv_editor_fx_vertex &above,
    rv_pdklib::fx near_plane,
    rv_editor_fx_vertex &cut)
{
    rv_pdklib::fx_vec3 p = {};
    const int rc = rv_pdklib::rv_gte_near_clip(below.p, above.p, near_plane, p);
    if (rc != RV_OK) {
        return rc;
    }
    // t is Q20.12; the uv and colour fraction is t / gte_one, in float.
    const std::int64_t t = rv_pdklib::rv_gte_near_t(below.p.z, above.p.z, near_plane);
    const float f = static_cast<float>(t) / static_cast<float>(rv_pdklib::gte_one);
    cut.p = p;
    cut.uv = ImVec2(lerp_f(below.uv.x, above.uv.x, f), lerp_f(below.uv.y, above.uv.y, f));
    cut.color = ImVec4(lerp_f(below.color.x, above.color.x, f),
        lerp_f(below.color.y, above.color.y, f),
        lerp_f(below.color.z, above.color.z, f),
        lerp_f(below.color.w, above.color.w, f));
    return RV_OK;
}

} // namespace

int rv_editor_clip_near_fx(const rv_editor_fx_tri &tri,
    rv_pdklib::fx near_plane,
    rv_editor_fx_tri (&out)[rv_editor_fx_max_pieces],
    int &count)
{
    count = 0;
    rv_editor_fx_vertex poly[max_corners] = {};
    int corners = 0;
    for (int i = 0; i < tri_vertices; ++i) {
        const rv_editor_fx_vertex &a = tri.v[i];
        const rv_editor_fx_vertex &b = tri.v[(i + 1) % tri_vertices];
        const bool a_in = a.p.z >= near_plane;
        const bool b_in = b.p.z >= near_plane;
        if (a_in) {
            poly[corners++] = a;
        }
        if (a_in != b_in) {
            const rv_editor_fx_vertex &below = a_in ? b : a;
            const rv_editor_fx_vertex &above = a_in ? a : b;
            const int rc = rv_editor_fx_cut(below, above, near_plane, poly[corners]);
            if (rc != RV_OK) {
                return rc;
            }
            ++corners;
        }
    }
    // Zero corners: wholly behind. Otherwise the polygon is convex; a fan from its first corner keeps the winding.
    count = corners < tri_vertices ? 0 : corners - 2;
    for (int k = 0; k < count; ++k) {
        out[k] = rv_editor_fx_tri{ { poly[0], poly[k + 1], poly[k + 2] } };
    }
    return RV_OK;
}

} // namespace rv_editor
