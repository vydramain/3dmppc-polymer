// The Scene viewport's geometry: the editor camera and what objects draw as.

#include "panes/rv_editor_scene_draw.hpp"

#include <cmath>
#include <functional>
#include <string_view>

#include "app/rv_editor_app.hpp"
#include "pdklib/rv_gte/rv_gte_trig.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "scene/rv_editor_scene.hpp"

namespace rv_editor
{

namespace
{

// Editor camera's full vertical field of view: twice the 25 degree half angle the frame used before,
// so the focal length h keeps its value.
constexpr double camera_fov_degrees = 50.0;
// Far clip plane, metres: past the camera's largest distance (200 m) with room for the scene.
constexpr double camera_far_metres = 1000.0;
// Field and far plane in Q20.12 for rv_gte_camera_make; the near plane is rv_editor_near_fx.
constexpr rv_pdklib::fx camera_fov_fx = static_cast<rv_pdklib::fx>(camera_fov_degrees * rv_pdklib::fx_one);
constexpr rv_pdklib::fx camera_far_fx = static_cast<rv_pdklib::fx>(camera_far_metres * rv_pdklib::fx_one);

// Half-extent of the placeholder unit cube (local coordinates).
constexpr double unit_half_extent = 0.5;
// Viewport centering: half of canvas dimension to find the center point.
constexpr float viewport_center_half = 0.5f;

// Number of vertices in a quad or rectangle.
constexpr int quad_vertex_count = 4;
// Number of corners in a cube.
constexpr int cube_corner_count = 8;
// Number of edges in a cube (edge pairs in the connectivity array).
constexpr int cube_edge_count = 12;
// Number of endpoints per edge definition.
constexpr int edge_ends = 2;
// Step size for iterating sign values from -1 to 1 (sign step = 2).
constexpr int sign_step = 2;
// Group marker: cube half-extent for corner brackets.
constexpr double group_marker_half_size = 0.25;
// Group marker: bracket leg length.
constexpr double group_marker_leg_length = 0.12;
// Group marker: central axis cross arm length.
constexpr double group_axis_cross_length = 0.15;

// Camera marker body: box half-width and half-height.
constexpr double camera_marker_box_half_size = 0.2;
// Camera marker body: depth back from origin.
constexpr double camera_marker_box_depth = 0.4;
// Camera marker lens: half-width of cone opening.
constexpr double camera_marker_lens_width = 0.3;
// Camera marker lens: half-height of cone opening.
constexpr double camera_marker_lens_height = 0.2;
// Camera marker lens: depth to cone opening face.
constexpr double camera_marker_lens_depth = 0.6;
// Camera marker lens: horizontal offset of up-mark center.
constexpr double camera_marker_up_mark_offset = 0.1;
// Camera marker lens: vertical position of up-mark base.
constexpr double camera_marker_up_mark_y = 0.25;
// Camera marker lens: vertical position of up-mark tip.
constexpr double camera_marker_up_mark_y_tip = 0.35;

// Octahedron marker (other kinds): half-extent on each axis.
constexpr double octahedron_marker_half_size = 0.3;
// Octahedron marker: number of vertices (top + bottom poles + 4 equatorial).
constexpr int octahedron_vertex_count = 6;

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

// The viewport's integer camera: up is +Y, the field is camera_fov_fx and the height is the frame's height in pixels.
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
        camera_fov_fx,
        static_cast<std::int32_t>(std::lround(height)),
        rv_editor_near_fx,
        camera_far_fx,
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

} // namespace

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

// Corner brackets of a small cube: three short legs per corner, pointing inward, plus
// a tiny axis cross through the centre. A group's marker: clearly not geometry.
void rv_editor_group_edges(std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    for (int sx = -1; sx <= 1; sx += sign_step) {
        for (int sy = -1; sy <= 1; sy += sign_step) {
            for (int sz = -1; sz <= 1; sz += sign_step) {
                const double x = sx * group_marker_half_size;
                const double y = sy * group_marker_half_size;
                const double z = sz * group_marker_half_size;
                const vec3 corner = p(x, y, z);
                e.emplace_back(corner, p(x - sx * group_marker_leg_length, y, z));
                e.emplace_back(corner, p(x, y - sy * group_marker_leg_length, z));
                e.emplace_back(corner, p(x, y, z - sz * group_marker_leg_length));
            }
        }
    }
    e.emplace_back(p(-group_axis_cross_length, 0, 0), p(group_axis_cross_length, 0, 0));
    e.emplace_back(p(0, -group_axis_cross_length, 0), p(0, group_axis_cross_length, 0));
    e.emplace_back(p(0, 0, -group_axis_cross_length), p(0, 0, group_axis_cross_length));
}

// A recognisable camera: a box body behind the origin plus a lens cone toward +Z, with an
// up mark on the lens rim so orientation reads.
void rv_editor_camera_edges(std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    const double bhs = camera_marker_box_half_size;
    const double bd = camera_marker_box_depth;
    const double lw = camera_marker_lens_width;
    const double lh = camera_marker_lens_height;
    const double ld = camera_marker_lens_depth;
    const vec3 b[cube_corner_count] = { p(-bhs, -bhs, -bd),
        p(bhs, -bhs, -bd),
        p(bhs, bhs, -bd),
        p(-bhs, bhs, -bd),
        p(-bhs, -bhs, 0),
        p(bhs, -bhs, 0),
        p(bhs, bhs, 0),
        p(-bhs, bhs, 0) };
    const int pairs[cube_edge_count][edge_ends] = { { 0, 1 },
        { 1, 2 },
        { 2, 3 },
        { 3, 0 },
        { 4, 5 },
        { 5, 6 },
        { 6, 7 },
        { 7, 4 },
        { 0, 4 },
        { 1, 5 },
        { 2, 6 },
        { 3, 7 } };
    for (const auto &pr : pairs) {
        e.emplace_back(b[pr[0]], b[pr[1]]);
    }
    const vec3 o = p(0, 0, 0);
    const vec3 c[quad_vertex_count] = { p(-lw, lh, ld), p(lw, lh, ld), p(lw, -lh, ld), p(-lw, -lh, ld) };
    for (int i = 0; i < quad_vertex_count; ++i) {
        e.emplace_back(o, c[i]);
        e.emplace_back(c[i], c[(i + 1) % quad_vertex_count]);
    }
    const double um_offset = camera_marker_up_mark_offset;
    const double um_y = camera_marker_up_mark_y;
    const double um_y_tip = camera_marker_up_mark_y_tip;
    e.emplace_back(p(-um_offset, um_y, ld), p(0, um_y_tip, ld)); // the up mark
    e.emplace_back(p(0, um_y_tip, ld), p(um_offset, um_y, ld));
}

// A volume: the unit cube's 12 edges, never filled.
void rv_editor_volume_edges(std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    const vec3 b[cube_corner_count] = { p(-unit_half_extent, -unit_half_extent, -unit_half_extent),
        p(unit_half_extent, -unit_half_extent, -unit_half_extent),
        p(unit_half_extent, unit_half_extent, -unit_half_extent),
        p(-unit_half_extent, unit_half_extent, -unit_half_extent),
        p(-unit_half_extent, -unit_half_extent, unit_half_extent),
        p(unit_half_extent, -unit_half_extent, unit_half_extent),
        p(unit_half_extent, unit_half_extent, unit_half_extent),
        p(-unit_half_extent, unit_half_extent, unit_half_extent) };
    const int pairs[cube_edge_count][edge_ends] = { { 0, 1 },
        { 1, 2 },
        { 2, 3 },
        { 3, 0 },
        { 4, 5 },
        { 5, 6 },
        { 6, 7 },
        { 7, 4 },
        { 0, 4 },
        { 1, 5 },
        { 2, 6 },
        { 3, 7 } };
    for (const auto &pr : pairs) {
        e.emplace_back(b[pr[0]], b[pr[1]]);
    }
}

// A diamond (octahedron), the third marker: for a kind that is neither mesh, camera nor group.
void rv_editor_other_edges(std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    const vec3 v[octahedron_vertex_count] = { p(octahedron_marker_half_size, 0, 0),
        p(-octahedron_marker_half_size, 0, 0),
        p(0, octahedron_marker_half_size, 0),
        p(0, -octahedron_marker_half_size, 0),
        p(0, 0, octahedron_marker_half_size),
        p(0, 0, -octahedron_marker_half_size) };
    // Top/bottom apexes (4, 5) to each of the four equatorial points (0..3).
    for (int apex = quad_vertex_count; apex < octahedron_vertex_count; ++apex) {
        for (int eq = 0; eq < quad_vertex_count; ++eq) {
            e.emplace_back(v[apex], v[eq]);
        }
    }
    e.emplace_back(v[0], v[2]);
    e.emplace_back(v[2], v[1]);
    e.emplace_back(v[1], v[3]);
    e.emplace_back(v[3], v[0]);
}

namespace
{

struct rv_editor_edge_builder {
    std::string_view kind;
    void (*builder)(std::vector<std::pair<vec3, vec3>> &, const std::function<vec3(double, double, double)> &);
};

constexpr rv_editor_edge_builder edge_builders[] = {
    { kind_camera, rv_editor_camera_edges },
    { kind_group, rv_editor_group_edges },
    { kind_volume, rv_editor_volume_edges },
};

} // namespace

std::vector<std::pair<vec3, vec3>>
rv_editor_object_edges(const rv_editor_scene &scene, const rv_editor_project &project, int index, std::string *error)
{
    const rv_editor_affine m = rv_editor_scene_world(scene, index);
    const std::string &kind = rv_editor_object_get_kind(scene.objects[static_cast<size_t>(index)]);
    auto p = [&m](double x, double y, double z) {
        return rv_editor_affine_point(m, { x, y, z });
    };
    std::vector<std::pair<vec3, vec3>> e;
    // A legacy quad or billboard has no model: its edges are the placeholder cube's, as a mesh without one.
    if (kind == kind_mesh || kind == kind_quad || kind == kind_billboard) {
        for (const rv_editor_tri &tri : rv_editor_object_triangles(scene, project, index, error)) {
            e.emplace_back(tri.p[0], tri.p[1]);
            e.emplace_back(tri.p[1], tri.p[2]);
            e.emplace_back(tri.p[2], tri.p[0]);
        }
        return e;
    }
    for (const auto &builder : edge_builders) {
        if (kind == builder.kind) {
            builder.builder(e, p);
            return e;
        }
    }
    rv_editor_other_edges(e, p);
    return e;
}

} // namespace rv_editor
