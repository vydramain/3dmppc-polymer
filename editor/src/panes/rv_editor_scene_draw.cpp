// The Scene viewport's geometry: the editor camera and what objects draw as.

#include "panes/rv_editor_scene_draw.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string_view>

#include "app/rv_editor_app.hpp"

namespace rv_editor
{

namespace
{

constexpr double rv_editor_deg = 3.14159265358979323846 / 180.0;

// Normalization threshold to avoid division by near-zero vectors.
constexpr double norm_threshold = 1e-12;
// Editor camera field of view angle.
constexpr double camera_fov_degrees = 25.0;

// Half-extent of placeholder geometric primitives (unit cube and quad, local coordinates).
constexpr double unit_half_extent = 0.5;
// Viewport centering: half of canvas dimension to find the center point.
constexpr float viewport_center_half = 0.5f;
// Focal length: half the viewport height for camera distance calculation.
constexpr double focal_length_half = 0.5;

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
// Number of spatial axes.
constexpr int axis_count = 3;

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

// Billboard marker: perpendicular tick length.
constexpr double billboard_marker_tick_length = 0.3;

// Octahedron marker (other kinds): half-extent on each axis.
constexpr double octahedron_marker_half_size = 0.3;
// Octahedron marker: number of vertices (top + bottom poles + 4 equatorial).
constexpr int octahedron_vertex_count = 6;

// Pointer proximity threshold for interactive picking and gizmo interaction.
constexpr float pointer_proximity_threshold_px = 6.0f;

vec3 cross(const vec3 &a, const vec3 &b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

vec3 norm(const vec3 &a)
{
    const double l = std::sqrt(dot(a, a));
    return l > norm_threshold ? mul(a, 1.0 / l) : a;
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
    const vec3 v = sub(p, eye);
    return { dot(v, right), dot(v, up), dot(v, forward) };
}

ImVec2 rv_editor_view::to_screen(const vec3 &v) const
{
    return ImVec2(center.x + static_cast<float>(v[0] / v[2] * focal), center.y - static_cast<float>(v[1] / v[2] * focal));
}

bool rv_editor_view::point(const vec3 &p, ImVec2 &out) const
{
    const vec3 v = to_view(p);
    if (v[2] < rv_editor_near) {
        return false;
    }
    out = to_screen(v);
    return true;
}

rv_editor_view rv_editor_view_make(const rv_editor_scene_camera &c, ImVec2 p0, ImVec2 size)
{
    rv_editor_view v;
    const double yaw = c.yaw * rv_editor_deg, pitch = c.pitch * rv_editor_deg;
    v.forward = { std::cos(pitch) * std::sin(yaw), -std::sin(pitch), std::cos(pitch) * std::cos(yaw) };
    v.eye = sub(c.target, mul(v.forward, c.distance));
    v.right = norm(cross({ 0.0, 1.0, 0.0 }, v.forward));
    v.up = cross(v.forward, v.right);
    v.center = ImVec2(p0.x + size.x * viewport_center_half, p0.y + size.y * viewport_center_half);
    v.focal = size.y * focal_length_half / std::tan(camera_fov_degrees * rv_editor_deg);
    return v;
}

void rv_editor_line(ImDrawList *dl, const rv_editor_view &v, const vec3 &a, const vec3 &b, ImU32 color, float width)
{
    vec3 va = v.to_view(a), vb = v.to_view(b);
    if (va[2] < rv_editor_near && vb[2] < rv_editor_near) {
        return;
    }
    if (va[2] < rv_editor_near || vb[2] < rv_editor_near) {
        const double t = (rv_editor_near - va[2]) / (vb[2] - va[2]);
        const vec3 cut = add(va, mul(sub(vb, va), t));
        (va[2] < rv_editor_near ? va : vb) = cut;
    }
    dl->AddLine(v.to_screen(va), v.to_screen(vb), color, width);
}

float rv_editor_segment_distance(ImVec2 p, ImVec2 a, ImVec2 b)
{
    const ImVec2 ab(b.x - a.x, b.y - a.y);
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    const float t = len2 > 0.0f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
    const float dx = a.x + ab.x * t - p.x, dy = a.y + ab.y * t - p.y;
    return std::sqrt(dx * dx + dy * dy);
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
    const vec3 b[cube_corner_count] = { p(-bhs, -bhs, -bd), p(bhs, -bhs, -bd), p(bhs, bhs, -bd),
        p(-bhs, bhs, -bd), p(-bhs, -bhs, 0), p(bhs, -bhs, 0), p(bhs, bhs, 0), p(-bhs, bhs, 0) };
    const int pairs[cube_edge_count][edge_ends] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
        { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
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

// The local XY unit square (-0.5..0.5): a quad's outline and a billboard's card.
void rv_editor_quad_edges(std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    const vec3 c[quad_vertex_count] = { p(-unit_half_extent, -unit_half_extent, 0), p(unit_half_extent, -unit_half_extent, 0),
        p(unit_half_extent, unit_half_extent, 0), p(-unit_half_extent, unit_half_extent, 0) };
    for (int i = 0; i < quad_vertex_count; ++i) {
        e.emplace_back(c[i], c[(i + 1) % quad_vertex_count]);
    }
}

// A billboard: the same card, plus a short tick off its face so the card does not read as flat.
void rv_editor_billboard_edges(
    std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    rv_editor_quad_edges(e, p);
    e.emplace_back(p(0, 0, 0), p(0, 0, billboard_marker_tick_length));
}

// A volume: the unit cube's 12 edges, never filled.
void rv_editor_volume_edges(
    std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    const vec3 b[cube_corner_count] = { p(-unit_half_extent, -unit_half_extent, -unit_half_extent),
        p(unit_half_extent, -unit_half_extent, -unit_half_extent),
        p(unit_half_extent, unit_half_extent, -unit_half_extent),
        p(-unit_half_extent, unit_half_extent, -unit_half_extent),
        p(-unit_half_extent, -unit_half_extent, unit_half_extent),
        p(unit_half_extent, -unit_half_extent, unit_half_extent),
        p(unit_half_extent, unit_half_extent, unit_half_extent),
        p(-unit_half_extent, unit_half_extent, unit_half_extent) };
    const int pairs[cube_edge_count][edge_ends] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
        { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (const auto &pr : pairs) {
        e.emplace_back(b[pr[0]], b[pr[1]]);
    }
}

// A diamond (octahedron), the third marker: for a kind that is neither mesh, camera nor group.
void rv_editor_other_edges(std::vector<std::pair<vec3, vec3>> &e, const std::function<vec3(double, double, double)> &p)
{
    const vec3 v[octahedron_vertex_count] = { p(octahedron_marker_half_size, 0, 0),
        p(-octahedron_marker_half_size, 0, 0), p(0, octahedron_marker_half_size, 0),
        p(0, -octahedron_marker_half_size, 0), p(0, 0, octahedron_marker_half_size),
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

std::vector<std::pair<vec3, vec3>> rv_editor_object_edges(
    const rv_editor_scene &scene, const rv_editor_project &project, int index, std::string *error)
{
    const rv_editor_affine m = rv_editor_scene_world(scene, index);
    const std::string &kind = scene.objects[static_cast<size_t>(index)].kind;
    auto p = [&m](double x, double y, double z) { return rv_editor_affine_point(m, { x, y, z }); };
    std::vector<std::pair<vec3, vec3>> e;
    if (kind == kind_mesh) {
        for (const rv_editor_tri &tri : rv_editor_object_triangles(scene, project, index, error)) {
            e.emplace_back(tri.p[0], tri.p[1]);
            e.emplace_back(tri.p[1], tri.p[2]);
            e.emplace_back(tri.p[2], tri.p[0]);
        }
    } else if (kind == kind_camera) {
        rv_editor_camera_edges(e, p);
    } else if (kind == kind_group) {
        rv_editor_group_edges(e, p);
    } else if (kind == kind_quad) {
        rv_editor_quad_edges(e, p);
    } else if (kind == kind_billboard) {
        rv_editor_billboard_edges(e, p);
    } else if (kind == kind_volume) {
        rv_editor_volume_edges(e, p);
    } else {
        rv_editor_other_edges(e, p);
    }
    return e;
}

std::string rv_editor_pick(
    const rv_editor_scene &scene, const rv_editor_project &project, const rv_editor_view &v, ImVec2 at)
{
    std::string best;
    float best_d = pointer_proximity_threshold_px;
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        for (const auto &[a, b] : rv_editor_object_edges(scene, project, static_cast<int>(i))) {
            ImVec2 sa, sb;
            if (!v.point(a, sa) || !v.point(b, sb)) {
                continue;
            }
            const float d = rv_editor_segment_distance(at, sa, sb);
            if (d < best_d) {
                best_d = d;
                best = scene.objects[i].id;
            }
        }
    }
    return best;
}

// The Move gizmo's axis under the mouse, or -1.
int rv_editor_gizmo_axis(const rv_editor_scene &scene, const std::string &selected, const rv_editor_view &v,
    ImVec2 mouse, double length)
{
    const int at = rv_editor_scene_find(scene, selected);
    if (at < 0) {
        return -1;
    }
    const vec3 o = rv_editor_affine_point(rv_editor_scene_world(scene, at), { 0, 0, 0 });
    ImVec2 so;
    if (!v.point(o, so)) {
        return -1;
    }
    for (int a = 0; a < axis_count; ++a) {
        vec3 tip = o;
        tip[static_cast<size_t>(a)] += length;
        ImVec2 st;
        if (v.point(tip, st) && rv_editor_segment_distance(mouse, so, st) < pointer_proximity_threshold_px) {
            return a;
        }
    }
    return -1;
}

} // namespace rv_editor
