// A mesh object's real geometry: its .glb or .obj file, cached and transformed into scene
// space. The filled pass that draws it is rv_editor_scene_raster.cpp.

#include "panes/rv_editor_scene_draw.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_map>

#include "imgui.h"

#include "pdk/rv_err.h"
#include "pdklib/rv_manifest/rv_manifest_pattern.hpp"
#include "pdklib/rv_math/rv_obj.hpp"

#include "panes/rv_editor_scene_glb.hpp"
#include "panes/rv_editor_walk.hpp"
#include "text/rv_editor_text.hpp"
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

vec3 to_vec3(const rv_pdklib::rv_vec3 &v)
{
    return { v.x, v.y, v.z };
}

void set_error(std::string *error, const std::string &text)
{
    if (error != nullptr) {
        *error = text;
    }
}

// One .obj, parsed once and kept until the file's mtime moves on.
struct rv_editor_mesh_cache_entry {
    std::filesystem::file_time_type mtime;
    bool ok = false;
    std::string error;
    rv_pdklib::rv_obj_mesh mesh;
};

// Keyed by absolute path; file-local so no other translation unit can reach into it.
std::unordered_map<std::string, rv_editor_mesh_cache_entry> rv_editor_mesh_cache;

// Loads and parses `path`, reusing the cached result while the file is unchanged.
// Returns null when the file cannot be read or does not parse; *error then explains why.
const rv_pdklib::rv_obj_mesh *rv_editor_mesh_load(const std::filesystem::path &path, std::string *error)
{
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(path, ec);
    const std::string key = path.string();
    if (ec) {
        set_error(error, path.filename().string() + ": not found");
        rv_editor_mesh_cache.erase(key);
        return nullptr;
    }
    auto it = rv_editor_mesh_cache.find(key);
    const bool cached = it != rv_editor_mesh_cache.end() && it->second.mtime == mtime;
    if (cached && !it->second.ok) {
        set_error(error, it->second.error);
        return nullptr;
    }
    if (cached) {
        return &it->second.mesh;
    }
    rv_editor_mesh_cache_entry entry;
    entry.mtime = mtime;
    std::ifstream in(path, std::ios::binary);
    std::ostringstream bytes;
    bytes << in.rdbuf();
    const std::string text = bytes.str();
    const bool read = static_cast<bool>(in);
    const bool parsed = read && rv_pdklib::rv_obj_parse(text.data(), text.size(), entry.mesh) == RV_OK;
    entry.ok = parsed;
    if (!parsed) {
        entry.error = path.filename().string() + (read ? ": does not parse" : ": cannot read");
    }
    auto &slot = rv_editor_mesh_cache[key] = std::move(entry);
    if (!slot.ok) {
        set_error(error, slot.error);
    }
    return slot.ok ? &slot.mesh : nullptr;
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

// A disc asset name has no folders (rv_dmain_setup.cpp refuses one with a separator;
// mppcburner names every [assets] entry by filename alone). So a mesh or texture name is
// a bare filename, resolved by walking the tree for it under a manifest pattern, not
// joined onto the root.

// The project file matching an [assets] pattern whose filename is `name`. Empty with
// *error set when the name has a folder in it or no such file is found.
std::filesystem::path rv_editor_resolve_mesh_name(const rv_editor_project &project, const std::string &name, std::string *error)
{
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        set_error(error, name + ": a disc asset name has no folders");
        return {};
    }
    const rv_editor_walk_result walk = rv_editor_walk_name(project, rv_editor_name_section::assets, false, name);
    if (!walk.matches.empty()) {
        return walk.matches.front();
    }
    if (walk.ec) {
        const std::string message = walk.ec.message();
        const int code = walk.ec.value();
        set_error(error, rv_editor_text_format("project.walk_failed", std::make_format_args(name, message, code)));
        return {};
    }
    set_error(error, name + ": no [assets] file by that name");
    return {};
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

std::vector<rv_editor_tri> rv_editor_object_triangles(const rv_editor_scene &scene,
    const rv_editor_project &project,
    int index,
    std::string *error,
    SDL_Renderer *renderer)
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
    if (o.mesh.ends_with(file_ext_glb)) {
        std::vector<rv_editor_tri> glb_tris;
        std::string glb_error;
        if (rv_editor_glb_object_triangles(project, o.mesh, m, renderer, glb_tris, glb_error) == RV_OK) {
            return glb_tris;
        }
        set_error(error, glb_error);
        return rv_editor_cube_triangles(m);
    }
    std::string resolve_error;
    const std::filesystem::path path = rv_editor_resolve_mesh_name(project, o.mesh, &resolve_error);
    if (path.empty()) {
        set_error(error, resolve_error);
        return rv_editor_cube_triangles(m);
    }
    std::string load_error;
    const rv_pdklib::rv_obj_mesh *mesh = rv_editor_mesh_load(path, &load_error);
    if (mesh == nullptr) {
        set_error(error, load_error);
        return rv_editor_cube_triangles(m);
    }
    std::vector<rv_editor_tri> tris;
    tris.reserve(mesh->triangles.size());
    for (const rv_pdklib::rv_obj_triangle &t : mesh->triangles) {
        rv_editor_tri tri;
        for (int k = 0; k < tri_vertices; ++k) {
            tri.p[k] = rv_editor_affine_point(m, to_vec3(rv_pdklib::rv_obj_position(*mesh, t.corner[k])));
            if (t.corner[k].uv >= 0) {
                const rv_pdklib::rv_vec2 uv = rv_pdklib::rv_obj_uv(*mesh, t.corner[k], { 0, 0 });
                tri.uv[k] = { uv.x, 1.0f - uv.y }; // .obj's bottom-left origin to ImGui's top-left
            } else {
                tri.uv[k] = { 0, 0 };
            }
        }
        tris.push_back(tri);
    }
    return tris;
}

} // namespace rv_editor
