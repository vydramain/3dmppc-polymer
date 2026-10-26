// The Scene viewport's edges per object: the wire of a mesh, quad or billboard, a volume's unit-cube wire,
// and the marker of a group, camera or other kind.

#include "panes/rv_editor_scene_draw.hpp"

#include <functional>
#include <string_view>

#include "scene/rv_editor_scene.hpp"

namespace rv_editor
{

namespace
{

// Half-extent of the placeholder unit cube (local coordinates).
constexpr double unit_half_extent = 0.5;

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

} // namespace

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

// A world point as Q20.12. Error: a coordinate outside Q20.12, NaN or inf; out is untouched.
int rv_editor_world_fx(const vec3 &p, rv_pdklib::fx_vec3 &out)
{
    rv_pdklib::fx_vec3 raw{};
    const int rc_x = rv_pdklib::fx_from_double(p[0], raw.x);
    const int rc_y = rv_pdklib::fx_from_double(p[1], raw.y);
    const int rc_z = rv_pdklib::fx_from_double(p[2], raw.z);
    if (rc_x != RV_OK || rc_y != RV_OK || rc_z != RV_OK) {
        return RV_ERR_INVAL;
    }
    out = raw;
    return RV_OK;
}

// A camera, group, volume or other kind's edges in scene space: its table builder, else the marker.
std::vector<std::pair<vec3, vec3>> rv_editor_marker_edges(const rv_editor_scene &scene, int index)
{
    const rv_editor_affine m = rv_editor_scene_world(scene, index);
    const std::string &kind = rv_editor_object_get_kind(scene.objects[static_cast<size_t>(index)]);
    auto p = [&m](double x, double y, double z) {
        return rv_editor_affine_point(m, { x, y, z });
    };
    std::vector<std::pair<vec3, vec3>> e;
    for (const auto &builder : edge_builders) {
        if (kind == builder.kind) {
            builder.builder(e, p);
            return e;
        }
    }
    rv_editor_other_edges(e, p);
    return e;
}

} // namespace

std::vector<std::pair<rv_pdklib::fx_vec3, rv_pdklib::fx_vec3>> rv_editor_object_edges_fx(const rv_editor_scene &scene,
    const rv_editor_project &project,
    int index,
    int &skipped,
    std::string *error)
{
    skipped = 0;
    const std::string &kind = rv_editor_object_get_kind(scene.objects[static_cast<size_t>(index)]);
    std::vector<std::pair<rv_pdklib::fx_vec3, rv_pdklib::fx_vec3>> e;
    if (kind == kind_mesh || kind == kind_quad || kind == kind_billboard) {
        for (const rv_editor_tri &tri : rv_editor_object_triangles(scene, project, index, error)) {
            // Without q the triangle's corners did not fit Q20.12; the raster refuses it too.
            if (!tri.q_valid) {
                ++skipped;
                continue;
            }
            e.emplace_back(tri.q[0], tri.q[1]);
            e.emplace_back(tri.q[1], tri.q[2]);
            e.emplace_back(tri.q[2], tri.q[0]);
        }
        return e;
    }
    for (const auto &[a, b] : rv_editor_marker_edges(scene, index)) {
        rv_pdklib::fx_vec3 fa{};
        rv_pdklib::fx_vec3 fb{};
        // Each end is converted once. An end outside Q20.12 skips its edge, as the raster refuses such a point.
        const int rc_a = rv_editor_world_fx(a, fa);
        const int rc_b = rv_editor_world_fx(b, fb);
        if (rc_a != RV_OK || rc_b != RV_OK) {
            ++skipped;
            continue;
        }
        e.emplace_back(fa, fb);
    }
    return e;
}

} // namespace rv_editor
