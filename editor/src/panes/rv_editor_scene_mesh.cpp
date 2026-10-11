// A mesh object's real geometry: its .glb file, transformed into scene space. The filled pass
// that draws it is rv_editor_scene_raster.cpp.

#include "panes/rv_editor_scene_draw.hpp"

#include <chrono>
#include <cmath>
#include <functional>

#include "imgui.h"

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest_pattern.hpp"

#include "panes/rv_editor_scene_glb.hpp"
#include "panes/rv_editor_walk.hpp"
#include "scene/rv_editor_scene_edit.hpp"

namespace rv_editor
{

namespace
{

// Half-extent of placeholder geometric primitives (unit cube and quad, local coordinates).
constexpr double unit_half_extent = 0.5;
// Corners of a unit cube.
constexpr int cube_corner_count = 8;
// Face definitions of a unit cube: six faces with four corner indices each.
constexpr std::array<std::array<int, 4>, 6> cube_faces = { {
    { 0, 1, 2, 3 },
    { 5, 4, 7, 6 },
    { 4, 0, 3, 7 },
    { 1, 5, 6, 2 },
    { 3, 2, 6, 7 },
    { 4, 5, 1, 0 },
} };
// Vertices of a quad.
constexpr int quad_vertex_count = 4;
// UV of a quad's corners: (0,0), (1,0), (1,1), (0,1).
constexpr std::array<ImVec2, quad_vertex_count> quad_uv_corners = { { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } };

void set_error(std::string *error, const std::string &text)
{
    if (error != nullptr) {
        *error = text;
    }
}

// The placeholder unit cube, twelve triangles, transformed by `m`.
std::vector<rv_editor_tri> rv_editor_cube_triangles(const rv_editor_affine &m)
{
    auto p = [&m](double x, double y, double z) {
        return rv_editor_affine_point(m, { x, y, z });
    };
    const vec3 c[cube_corner_count] = { p(-unit_half_extent, -unit_half_extent, -unit_half_extent),
        p(unit_half_extent, -unit_half_extent, -unit_half_extent),
        p(unit_half_extent, unit_half_extent, -unit_half_extent),
        p(-unit_half_extent, unit_half_extent, -unit_half_extent),
        p(-unit_half_extent, -unit_half_extent, unit_half_extent),
        p(unit_half_extent, -unit_half_extent, unit_half_extent),
        p(unit_half_extent, unit_half_extent, unit_half_extent),
        p(-unit_half_extent, unit_half_extent, unit_half_extent) };
    std::vector<rv_editor_tri> tris;
    for (const auto &f : cube_faces) {
        tris.push_back({ { c[f[0]], c[f[1]], c[f[2]] }, { quad_uv_corners[0], quad_uv_corners[1], quad_uv_corners[2] } });
        tris.push_back({ { c[f[0]], c[f[2]], c[f[3]] }, { quad_uv_corners[0], quad_uv_corners[2], quad_uv_corners[3] } });
    }
    return tris;
}

// The four corners' pixel-rect uv, top-left, top-right, bottom-left, bottom-right, from an
// object's `uv` (u0,v0,u1,v1). Raw pixels; rv_editor_draw_filled normalizes once a texture's
// pixel size is known, or leaves them unused when the object draws flat.
std::array<ImVec2, quad_vertex_count> rv_editor_quad_uv(const rv_editor_uv &uv)
{
    return { ImVec2{ static_cast<float>(uv[0]), static_cast<float>(uv[1]) },
        ImVec2{ static_cast<float>(uv[2]), static_cast<float>(uv[1]) },
        ImVec2{ static_cast<float>(uv[0]), static_cast<float>(uv[3]) },
        ImVec2{ static_cast<float>(uv[2]), static_cast<float>(uv[3]) } };
}

// A quad's two triangles: rv_pdklib::rv_scene_quad_corners' own local square and corner
// order (top-left, top-right, bottom-left, bottom-right; scale is already in `m`).
std::vector<rv_editor_tri> rv_editor_quad_triangles(const rv_editor_affine &m, const rv_editor_uv &uv)
{
    const vec3 c[4] = { rv_editor_affine_point(m, { -unit_half_extent, unit_half_extent, 0.0 }),
        rv_editor_affine_point(m, { unit_half_extent, unit_half_extent, 0.0 }),
        rv_editor_affine_point(m, { -unit_half_extent, -unit_half_extent, 0.0 }),
        rv_editor_affine_point(m, { unit_half_extent, -unit_half_extent, 0.0 }) };
    const std::array<ImVec2, quad_vertex_count> t = rv_editor_quad_uv(uv);
    return {
        { { c[0], c[2], c[1] }, { t[0], t[2], t[1] } },
        { { c[1], c[2], c[3] }, { t[1], t[2], t[3] } },
    };
}

// The world-space length of `m`'s column `col` (0: X, 1: Y): the parent chain's scale and
// rotation folded in, unlike the object's own local `scale`.
double rv_editor_affine_column_length(const rv_editor_affine &m, int col)
{
    return std::sqrt(m[0][col] * m[0][col] + m[1][col] * m[1][col] + m[2][col] * m[2][col]);
}

} // namespace

std::vector<rv_editor_tri>
rv_editor_billboard_triangles(const rv_editor_view &v, const rv_editor_affine &m, const rv_editor_scene_object &o)
{
    const vec3 center = rv_editor_affine_point(m, { 0.0, 0.0, 0.0 });
    const vec3 right = mul(v.right, rv_editor_affine_column_length(m, 0) * unit_half_extent);
    const vec3 up = mul(v.up, rv_editor_affine_column_length(m, 1) * unit_half_extent);
    const vec3 c[4] = { add(center, sub(up, right)),
        add(center, add(up, right)),
        sub(center, add(up, right)),
        add(center, sub(right, up)) };
    const std::array<ImVec2, quad_vertex_count> t = rv_editor_quad_uv(o.uv);
    return {
        { { c[0], c[2], c[1] }, { t[0], t[2], t[1] } },
        { { c[1], c[2], c[3] }, { t[1], t[2], t[3] } },
    };
}

// A scene's `texture` is a flat disc texture name (mppcburner: the source's stem plus
// ".mppctex"), resolved to the project file under [textures] whose stem matches.
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
    if (o.kind == kind_quad) {
        return rv_editor_quad_triangles(rv_editor_scene_world(scene, index), o.uv);
    }
    if (o.kind != kind_mesh) {
        return {};
    }
    const rv_editor_affine m = rv_editor_scene_world(scene, index);
    if (o.mesh.empty()) {
        return rv_editor_cube_triangles(m);
    }
    if (!o.mesh.ends_with(file_ext_glb)) {
        set_error(error, o.mesh + ": only .glb meshes are supported");
        return rv_editor_cube_triangles(m);
    }
    std::vector<rv_editor_tri> glb_tris;
    std::string glb_error;
    if (rv_editor_glb_object_triangles(project, o.mesh, m, glb_tris, glb_error) != RV_OK) {
        set_error(error, glb_error);
        return rv_editor_cube_triangles(m);
    }
    return glb_tris;
}

} // namespace rv_editor
