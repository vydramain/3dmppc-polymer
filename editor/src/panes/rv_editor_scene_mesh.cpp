// A mesh object's real geometry: its .glb file, transformed into scene space. The filled pass
// that draws it is rv_editor_scene_raster.cpp.

#include "panes/rv_editor_scene_draw.hpp"

#include <chrono>
#include <functional>

#include "imgui.h"

#include "pdk/rv_err.h"
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

} // namespace

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
    const std::string kind = rv_editor_object_get_kind(o);
    if (kind == kind_quad || kind == kind_billboard) {
        // A legacy quad or billboard has no model: the placeholder box, as a source-less mesh.
        return rv_editor_cube_triangles(rv_editor_scene_world(scene, index));
    }
    if (kind != kind_mesh) {
        return {};
    }
    const rv_editor_affine m = rv_editor_scene_world(scene, index);
    const std::string mesh = rv_editor_object_get_mesh(o);
    if (mesh.empty()) {
        return rv_editor_cube_triangles(m);
    }
    if (!rv_editor_is_model_file_name(mesh)) {
        set_error(error, rv_editor_text_format("project.mesh_not_supported", std::make_format_args(mesh)));
        return rv_editor_cube_triangles(m);
    }
    std::vector<rv_editor_tri> glb_tris;
    std::string glb_error;
    if (rv_editor_glb_object_triangles(project, mesh, m, glb_tris, glb_error) != RV_OK) {
        set_error(error, glb_error);
        return rv_editor_cube_triangles(m);
    }
    return glb_tris;
}

} // namespace rv_editor
