// The Scene viewport's geometry: the editor camera and what objects draw as.

#include "panes/rv_editor_scene_draw.hpp"

#include <cmath>

#include "app/rv_editor_app.hpp"
#include "pdklib/rv_gte/rv_gte_project.hpp"
#include "pdklib/rv_gte/rv_gte_trig.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "scene/rv_editor_scene.hpp"

namespace rv_editor
{

namespace
{

// Viewport centering: half of canvas dimension to find the center point.
constexpr float viewport_center_half = 0.5f;

// The view direction from yaw and pitch (raw Q20.12 degrees), in 4.12 like the trig tables:
// (cos pitch sin yaw, -sin pitch, cos pitch cos yaw).
rv_pdklib::fx_vec3 rv_editor_forward_fx(rv_pdklib::fx yaw, rv_pdklib::fx pitch)
{
    const rv_pdklib::fx sin_y = rv_pdklib::rv_gte_sin(rv_pdklib::rv_gte_angle_index(yaw));
    const rv_pdklib::fx cos_y = rv_pdklib::rv_gte_cos(rv_pdklib::rv_gte_angle_index(yaw));
    const rv_pdklib::fx sin_p = rv_pdklib::rv_gte_sin(rv_pdklib::rv_gte_angle_index(pitch));
    const rv_pdklib::fx cos_p = rv_pdklib::rv_gte_cos(rv_pdklib::rv_gte_angle_index(pitch));
    return { static_cast<rv_pdklib::fx>(rv_pdklib::rv_gte_round_frac(static_cast<std::int64_t>(cos_p) * sin_y)),
        -sin_p,
        static_cast<rv_pdklib::fx>(rv_pdklib::rv_gte_round_frac(static_cast<std::int64_t>(cos_p) * cos_y)) };
}

// Target and eye in Q20.12 metres: the eye stands distance back from the target along the forward direction.
// Error: a camera value outside Q20.12.
int rv_editor_eye_fx(const rv_editor_scene_camera &c, rv_pdklib::fx_vec3 &target, rv_pdklib::fx_vec3 &eye)
{
    rv_pdklib::fx yaw = 0;
    rv_pdklib::fx pitch = 0;
    rv_pdklib::fx distance = 0;
    if (const int rc = rv_editor_camera_get_yaw_fx(c, yaw); rc != RV_OK) {
        return rc;
    }
    if (const int rc = rv_editor_camera_get_pitch_fx(c, pitch); rc != RV_OK) {
        return rc;
    }
    if (const int rc = rv_editor_camera_get_distance_fx(c, distance); rc != RV_OK) {
        return rc;
    }
    if (const int rc = rv_editor_camera_get_target_fx(c, target); rc != RV_OK) {
        return rc;
    }
    rv_pdklib::fx_vec3 back{};
    if (const int rc = rv_pdklib::fx_vec3_scale(rv_editor_forward_fx(yaw, pitch), distance, back); rc != RV_OK) {
        return rc;
    }
    return rv_pdklib::fx_vec3_sub(target, back, eye);
}

// The viewport's integer camera: up is +Y, the field is rv_editor_camera_fov_fx and the height is the frame's height in pixels.
// Error: a value outside Q20.12, or a camera rv_gte_camera_make refuses; out is untouched.
int rv_editor_camera_build(const rv_editor_scene_camera &c, float height, rv_pdklib::rv_gte_camera &out)
{
    rv_pdklib::fx_vec3 target{};
    rv_pdklib::fx_vec3 eye{};
    if (const int rc = rv_editor_eye_fx(c, target, eye); rc != RV_OK) {
        return rc;
    }
    const rv_pdklib::fx_vec3 up{ 0, rv_pdklib::fx_one, 0 };
    return rv_pdklib::rv_gte_camera_make(eye,
        target,
        up,
        rv_editor_camera_fov_fx,
        static_cast<std::int32_t>(std::lround(height)),
        rv_editor_near_fx,
        rv_editor_camera_far_fx,
        out);
}

// A view-space point in Q20.12. Error: a coordinate outside Q20.12; p is untouched.
int rv_editor_fx_of_view(const vec3 &v, rv_pdklib::fx_vec3 &p)
{
    rv_pdklib::fx_vec3 raw{};
    if (rv_pdklib::fx_from_double(v[0], raw.x) != RV_OK || rv_pdklib::fx_from_double(v[1], raw.y) != RV_OK ||
        rv_pdklib::fx_from_double(v[2], raw.z) != RV_OK) {
        return RV_ERR_INVAL;
    }
    p = raw;
    return RV_OK;
}

// Centre-relative pixels of a Q20.12 view point, rounded as the console's rv_gte_project; the float centre is added after.
// Precondition: p.z > 0; a failure returns the centre.
ImVec2 rv_editor_screen_of_fx(const rv_editor_view &v, const rv_pdklib::fx_vec3 &p)
{
    rv_pdklib::rv_gte_screen s{};
    if (rv_pdklib::rv_gte_project(p, { v.camera.h, 0, 0 }, s) != RV_OK) {
        return v.center;
    }
    return ImVec2(v.center.x + static_cast<float>(s.x), v.center.y + static_cast<float>(s.y));
}

// Cuts the behind end of an edge that crosses the near plane to the plane; an edge wholly on one side is left as it is.
// Error: never for a crossing edge (t is in [0, 4096]); the behind end is then untouched.
int rv_editor_cut_edge(rv_pdklib::fx_vec3 &a, rv_pdklib::fx_vec3 &b)
{
    const bool a_behind = a.z < rv_editor_near_fx;
    if (a_behind == (b.z < rv_editor_near_fx)) {
        return RV_OK;
    }
    rv_pdklib::fx_vec3 &behind = a_behind ? a : b;
    const rv_pdklib::fx_vec3 front = a_behind ? b : a;
    return rv_pdklib::rv_gte_near_clip(behind, front, rv_editor_near_fx, behind);
}

// Both ends of an edge in Q20.12, the crossing one cut to the near plane. Error: a view point outside Q20.12.
int rv_editor_edge_fx(const rv_editor_view &v, const vec3 &a, const vec3 &b, rv_pdklib::fx_vec3 &pa, rv_pdklib::fx_vec3 &pb)
{
    if (rv_editor_fx_of_view(v.to_view(a), pa) != RV_OK || rv_editor_fx_of_view(v.to_view(b), pb) != RV_OK) {
        return RV_ERR_INVAL;
    }
    return rv_editor_cut_edge(pa, pb);
}

// A world point in Q20.12 taken to view space by the integer camera. Error: the product leaves int32; out is untouched.
int rv_editor_point_view_fx(const rv_editor_view &v, const rv_pdklib::fx_vec3 &scene, rv_pdklib::fx_vec3 &out)
{
    const std::int32_t in[tri_vertices] = { scene.x, scene.y, scene.z };
    std::int32_t view[tri_vertices] = {};
    if (rv_pdklib::rv_gte_point(v.camera.view, in, view) != RV_OK) {
        return RV_ERR_INVAL;
    }
    out = { view[0], view[1], view[2] };
    return RV_OK;
}

} // namespace

// The cull mode of an object as pdkkit's rv_ecs_frame.hpp gives it (two_sided -> none, else back, near line 113),
// which the console's gte_cull_mode in src/rv_pconsole/cg/rv_pccg_sw_put.cpp copies.
std::uint32_t rv_editor_cull_mode(bool two_sided)
{
    return two_sided ? rv_pdklib::gte_cull_none : rv_pdklib::gte_cull_screen_cw;
}

// Exact while the corners lie within +-gte_screen_limit, the range rv_gte_project returns: beyond it rv_gte_clamp_screen
// can change the sign. Zero area is dropped in every mode, as the console does.
bool rv_editor_screen_culled(const rv_pdklib::rv_gte_screen (&s)[tri_vertices], std::uint32_t cull)
{
    const bool zero_area = rv_pdklib::rv_gte_culled(s[0], s[1], s[2], rv_pdklib::gte_cull_screen_cw) &&
        rv_pdklib::rv_gte_culled(s[0], s[1], s[2], rv_pdklib::gte_cull_screen_ccw);
    return zero_area || rv_pdklib::rv_gte_culled(s[0], s[1], s[2], cull);
}

vec3 sub(const vec3 &a, const vec3 &b)
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}
vec3 add(const vec3 &a, const vec3 &b)
{
    return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
}
vec3 mul(const vec3 &a, double s)
{
    return { a[0] * s, a[1] * s, a[2] * s };
}
double dot(const vec3 &a, const vec3 &b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

vec3 rv_editor_view::to_view(const vec3 &p) const
{
    std::int32_t in[3] = {};
    for (int k = 0; k < 3; ++k) {
        rv_pdklib::fx raw = 0;
        // Outside Q20.12 or int32 the point counts as behind the near plane: {} has z 0.
        if (rv_pdklib::fx_from_double(p[k], raw) != RV_OK) {
            return {};
        }
        in[k] = raw;
    }
    std::int32_t out[3] = {};
    if (rv_pdklib::rv_gte_point(camera.view, in, out) != RV_OK) {
        return {};
    }
    return { rv_editor_camera_units_of(out[0]), rv_editor_camera_units_of(out[1]), rv_editor_camera_units_of(out[2]) };
}

ImVec2 rv_editor_view::to_screen(const vec3 &v) const
{
    // Precondition: v[2] > 0 (the callers clip at the near plane); a failure returns the centre.
    rv_pdklib::fx_vec3 p{};
    return rv_editor_fx_of_view(v, p) == RV_OK ? rv_editor_screen_of_fx(*this, p) : center;
}

bool rv_editor_view::point(const vec3 &p, ImVec2 &out) const
{
    // A point outside Q20.12 is not on screen: it returns false.
    rv_pdklib::fx_vec3 v{};
    if (rv_editor_fx_of_view(to_view(p), v) != RV_OK || v.z < rv_editor_near_fx) {
        return false;
    }
    out = rv_editor_screen_of_fx(*this, v);
    return true;
}

rv_editor_view rv_editor_view_make(const rv_editor_scene_camera &c, ImVec2 p0, ImVec2 size)
{
    // Reported once per failure: the flag clears when the camera builds again.
    static bool reported = false;
    rv_editor_view v;
    v.center = ImVec2(p0.x + size.x * viewport_center_half, p0.y + size.y * viewport_center_half);
    if (const int rc = rv_editor_camera_build(c, size.y, v.camera); rc != RV_OK) {
        if (!reported) {
            reported = true;
            RV_LOG_WARN(scene_log_tag, "viewport camera is out of range (rc {}): the scene is not drawn", rc);
        }
        return v;
    }
    reported = false;
    v.valid = true;
    // The rows are the frame: right, up, forward, each exact in 4.12.
    for (int i = 0; i < 3; ++i) {
        v.right[i] = rv_editor_camera_units_of(v.camera.view.r[i]);
        v.up[i] = rv_editor_camera_units_of(v.camera.view.r[3 + i]);
        v.forward[i] = rv_editor_camera_units_of(v.camera.view.r[6 + i]);
    }
    return v;
}

void rv_editor_line(ImDrawList *dl, const rv_editor_view &v, const vec3 &a, const vec3 &b, ImU32 color, float width)
{
    // View-space overflow skips the edge; input overflow counts as behind, as in to_view.
    static bool reported = false;
    rv_pdklib::fx_vec3 pa{};
    rv_pdklib::fx_vec3 pb{};
    if (rv_editor_edge_fx(v, a, b, pa, pb) != RV_OK) {
        if (!reported) {
            reported = true;
            RV_LOG_WARN(scene_log_tag, "a scene edge is outside Q20.12: the edge is not drawn");
        }
        return;
    }
    if (pa.z < rv_editor_near_fx && pb.z < rv_editor_near_fx) {
        return;
    }
    dl->AddLine(rv_editor_screen_of_fx(v, pa), rv_editor_screen_of_fx(v, pb), color, width);
}

int rv_editor_edge_screen_fx(const rv_editor_view &v,
    const rv_pdklib::fx_vec3 &a,
    const rv_pdklib::fx_vec3 &b,
    ImVec2 &sa,
    ImVec2 &sb)
{
    rv_pdklib::fx_vec3 pa{};
    rv_pdklib::fx_vec3 pb{};
    const int rc_a = rv_editor_point_view_fx(v, a, pa);
    const int rc_b = rv_editor_point_view_fx(v, b, pb);
    const int rc_cut = rv_editor_cut_edge(pa, pb);
    if (rc_a != RV_OK || rc_b != RV_OK || rc_cut != RV_OK) {
        return RV_ERR_INVAL;
    }
    // Both ends still behind after the cut: the edge has no visible side. Not a fault, so no log.
    if (pa.z < rv_editor_near_fx && pb.z < rv_editor_near_fx) {
        return RV_ERR_NOENT;
    }
    sa = rv_editor_screen_of_fx(v, pa);
    sb = rv_editor_screen_of_fx(v, pb);
    return RV_OK;
}

void rv_editor_line_fx(ImDrawList *dl,
    const rv_editor_view &v,
    const rv_pdklib::fx_vec3 &a,
    const rv_pdklib::fx_vec3 &b,
    ImU32 color,
    float width)
{
    // Its own once flag, apart from rv_editor_line's: a view-space overflow is skipped and logged once, as there.
    static bool reported = false;
    ImVec2 sa{};
    ImVec2 sb{};
    const int rc = rv_editor_edge_screen_fx(v, a, b, sa, sb);
    if (rc == RV_OK) {
        dl->AddLine(sa, sb, color, width);
        return;
    }
    if (rc != RV_ERR_INVAL || reported) {
        return;
    }
    reported = true;
    RV_LOG_WARN(scene_log_tag, "a scene edge is outside Q20.12: the edge is not drawn");
}

} // namespace rv_editor
