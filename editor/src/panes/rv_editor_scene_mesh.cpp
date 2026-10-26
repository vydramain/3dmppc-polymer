// A mesh object's real geometry: its .glb file, transformed into scene space. The filled pass
// that draws it is rv_editor_scene_raster.cpp.

#include "panes/rv_editor_scene_draw.hpp"

#include <chrono>
#include <cstdint>
#include <functional>

#include "imgui.h"

#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_manifest/rv_manifest_pattern.hpp"

#include "panes/rv_editor_scene_glb.hpp"
#include "panes/rv_editor_walk.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

// Half-extent of the placeholder unit cube (local coordinates).
constexpr double unit_half_extent = 0.5;
// The same half-extent in Q20.12: exact, so the cube's corners are integer points before the world matrix.
constexpr std::int32_t cube_half_raw = static_cast<std::int32_t>(unit_half_extent * static_cast<double>(rv_pdklib::gte_one));
// Corners of a unit cube.
constexpr int cube_corner_count = 8;
// Signs of each corner's x, y, z, in the order cube_faces indexes them.
constexpr std::array<std::array<int, 3>, cube_corner_count> cube_corner_signs = {
    { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 }, { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } }
};
// Face definitions of a unit cube: six faces with four corner indices each.
constexpr std::array<std::array<int, 4>, 6> cube_faces = { {
    { 0, 1, 2, 3 },
    { 5, 4, 7, 6 },
    { 4, 0, 3, 7 },
    { 1, 5, 6, 2 },
    { 3, 2, 6, 7 },
    { 4, 5, 1, 0 },
} };
// Corners of a cube face: each face's two triangles take four uv corners.
constexpr int quad_vertex_count = 4;
// UV of a cube face's corners: (0,0), (1,0), (1,1), (0,1).
constexpr std::array<ImVec2, quad_vertex_count> quad_uv_corners = { { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } };

void set_error(std::string *error, const std::string &text)
{
    if (error != nullptr) {
        *error = text;
    }
}

// A placeholder cube corner: its scene-space metres, its Q20.12 raw, and whether that raw fit.
struct cube_corner {
    vec3 p{};
    rv_pdklib::fx_vec3 q{};
    bool q_valid = false;
};

// The triangle of corners idx of c, with uv in the same order. q_valid is set only when all three corners fit
// Q20.12; q is copied either way.
rv_editor_tri cube_tri(const cube_corner (&c)[cube_corner_count],
    const std::array<int, tri_vertices> &idx,
    const std::array<ImVec2, tri_vertices> &uv)
{
    rv_editor_tri tri;
    tri.q_valid = true;
    for (int k = 0; k < tri_vertices; ++k) {
        const cube_corner &corner = c[idx[k]];
        tri.q[k] = corner.q;
        tri.uv[k] = uv[k];
        tri.q_valid = tri.q_valid && corner.q_valid;
    }
    return tri;
}

// The placeholder cube's twelve triangles from its eight scene-space corners.
std::vector<rv_editor_tri> cube_triangles(const cube_corner (&c)[cube_corner_count])
{
    std::vector<rv_editor_tri> tris;
    for (const auto &f : cube_faces) {
        tris.push_back(cube_tri(c, { f[0], f[1], f[2] }, { quad_uv_corners[0], quad_uv_corners[1], quad_uv_corners[2] }));
        tris.push_back(cube_tri(c, { f[0], f[2], f[3] }, { quad_uv_corners[0], quad_uv_corners[2], quad_uv_corners[3] }));
    }
    return tris;
}

// Q20.12 raw of a metres point, each component rounded to the nearest step. RV_OK, or fx_from_double's
// RV_ERR_INVAL when one leaves fx; q is then untouched.
int fx_point_from_metres(const vec3 &p, rv_pdklib::fx_vec3 &q)
{
    rv_pdklib::fx_vec3 raw{};
    if (const int rc = rv_pdklib::fx_from_double(p[0], raw.x); rc != RV_OK) {
        return rc;
    }
    if (const int rc = rv_pdklib::fx_from_double(p[1], raw.y); rc != RV_OK) {
        return rc;
    }
    if (const int rc = rv_pdklib::fx_from_double(p[2], raw.z); rc != RV_OK) {
        return rc;
    }
    q = raw;
    return RV_OK;
}

// The placeholder cube through the double affine `m`: the fallback when the integer world cannot be built.
std::vector<rv_editor_tri> rv_editor_cube_triangles(const rv_editor_affine &m)
{
    cube_corner c[cube_corner_count] = {};
    for (int i = 0; i < cube_corner_count; ++i) {
        const auto &s = cube_corner_signs[i];
        c[i].p = rv_editor_affine_point(m, { s[0] * unit_half_extent, s[1] * unit_half_extent, s[2] * unit_half_extent });
        c[i].q_valid = fx_point_from_metres(c[i].p, c[i].q) == RV_OK;
    }
    return cube_triangles(c);
}

// The placeholder cube through the integer `world`: each corner is a Q20.12 point. A corner that
// leaves int32 returns RV_ERR_INVAL with the reason in `error`; `tris` is then left empty.
int rv_editor_cube_gte_triangles(const rv_pdklib::rv_gte_matrix &world,
    const std::string &name,
    std::vector<rv_editor_tri> &tris,
    std::string &error)
{
    cube_corner c[cube_corner_count] = {};
    for (int i = 0; i < cube_corner_count; ++i) {
        const auto &s = cube_corner_signs[i];
        const std::int32_t local[3] = { s[0] * cube_half_raw, s[1] * cube_half_raw, s[2] * cube_half_raw };
        std::int32_t raw[3] = {};
        if (rv_pdklib::rv_gte_point(world, local, raw) != RV_OK) {
            error = rv_editor_text_format("project.cube_corner_out_of_range", std::make_format_args(name));
            return RV_ERR_INVAL;
        }
        c[i].q = { raw[0], raw[1], raw[2] };
        c[i].q_valid = true;
    }
    tris = cube_triangles(c);
    return RV_OK;
}

// The integer cube for `rv_editor_object_triangles`. A refused corner's reason is shown only when `error` holds no
// earlier one, so the first reason stays.
std::vector<rv_editor_tri> cube_or_undrawn(const rv_pdklib::rv_gte_matrix &world, const std::string &name, std::string *error)
{
    std::vector<rv_editor_tri> tris;
    std::string cube_error;
    if (rv_editor_cube_gte_triangles(world, name, tris, cube_error) != RV_OK && error != nullptr && error->empty()) {
        *error = cube_error;
    }
    return tris;
}

} // namespace

// A scene's `texture` is a flat disc texture name (mppcburner: the source's stem plus
// ".mppctex"), resolved to the project file among the [assets] textures whose stem matches.
std::filesystem::path rv_editor_resolve_texture_name(const rv_editor_project &project, const std::string &name)
{
    const std::string stem = std::filesystem::path(name).stem().string();
    const rv_editor_walk_result walk = rv_editor_walk_name(project, rv_editor_name_section::textures, true, stem);
    return walk.matches.empty() ? std::filesystem::path() : walk.matches.front();
}

std::vector<rv_editor_tri>
rv_editor_object_triangles(const rv_editor_scene &scene, const rv_editor_project &project, int index, std::string *error)
{
    const rv_editor_scene_object &o = scene.objects[static_cast<size_t>(index)];
    const std::string kind = rv_editor_object_get_kind(o);
    if (kind != kind_quad && kind != kind_billboard && kind != kind_mesh) {
        return {};
    }
    const std::string name = rv_editor_object_get_name(o);
    rv_pdklib::rv_gte_matrix world = {};
    if (rv_editor_scene_world_gte(scene, index, world) != RV_OK) {
        set_error(error, rv_editor_text_format("project.object_world_out_of_range", std::make_format_args(name)));
        return rv_editor_cube_triangles(rv_editor_scene_world(scene, index));
    }
    // A legacy quad or billboard has no model: the placeholder box, as a source-less mesh.
    const std::string mesh = rv_editor_object_get_mesh(o);
    if (kind != kind_mesh || mesh.empty()) {
        return cube_or_undrawn(world, name, error);
    }
    if (!rv_editor_is_model_file_name(mesh)) {
        set_error(error, rv_editor_text_format("project.mesh_not_supported", std::make_format_args(mesh)));
        return cube_or_undrawn(world, name, error);
    }
    std::vector<rv_editor_tri> glb_tris;
    std::string glb_error;
    if (rv_editor_glb_object_triangles(project, mesh, world, glb_tris, glb_error) != RV_OK) {
        set_error(error, glb_error);
        return cube_or_undrawn(world, name, error);
    }
    // A warning from a model with triangles not drawn; the triangles that fit are still shown.
    if (!glb_error.empty()) {
        set_error(error, glb_error);
    }
    return glb_tris;
}

} // namespace rv_editor
