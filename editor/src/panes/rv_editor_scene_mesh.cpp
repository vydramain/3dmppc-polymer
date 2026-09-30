// A mesh object's real geometry: its .obj file, cached and transformed into scene
// space, and the filled-polygon draw mode built on top of it.

#include "panes/rv_editor_scene_draw.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "imgui.h"

#include "pdklib/rv_manifest/rv_manifest_pattern.hpp"
#include "pdklib/rv_math/rv_obj.hpp"

#include "scene/rv_editor_scene_edit.hpp"

namespace rv_editor
{

namespace
{

vec3 cross(const vec3 &a, const vec3 &b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

vec3 norm(const vec3 &a)
{
    const double l = std::sqrt(dot(a, a));
    return l > 1e-12 ? mul(a, 1.0 / l) : a;
}

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
struct rv_editor_mesh_cache_entry
{
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
    if (it != rv_editor_mesh_cache.end() && it->second.mtime == mtime) {
        if (!it->second.ok) {
            set_error(error, it->second.error);
        }
        return it->second.ok ? &it->second.mesh : nullptr;
    }
    rv_editor_mesh_cache_entry entry;
    entry.mtime = mtime;
    std::ifstream in(path, std::ios::binary);
    std::ostringstream bytes;
    bytes << in.rdbuf();
    const std::string text = bytes.str();
    entry.ok = in && rv_pdklib::rv_obj_parse(text.data(), text.size(), entry.mesh);
    if (!entry.ok) {
        entry.error = path.filename().string() + ": does not parse";
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
    auto p = [&m](double x, double y, double z) { return rv_editor_affine_point(m, { x, y, z }); };
    const double h = 0.5;
    const vec3 c[8] = { p(-h, -h, -h), p(h, -h, -h), p(h, h, -h), p(-h, h, -h), p(-h, -h, h), p(h, -h, h),
        p(h, h, h), p(-h, h, h) };
    const int faces[6][4] = {
        { 0, 1, 2, 3 }, { 5, 4, 7, 6 }, { 4, 0, 3, 7 }, { 1, 5, 6, 2 }, { 3, 2, 6, 7 }, { 4, 5, 1, 0 },
    };
    std::vector<rv_editor_tri> tris;
    for (const auto &f : faces) {
        tris.push_back({ { c[f[0]], c[f[1]], c[f[2]] } });
        tris.push_back({ { c[f[0]], c[f[2]], c[f[3]] } });
    }
    return tris;
}

// A disc asset name has no folders (rv_dmain_setup.cpp refuses one with a separator;
// mppcburner names every [assets] entry by filename alone). So `mesh` is a bare filename,
// resolved by walking the tree for it under an [assets] pattern, not joined onto the root.
struct rv_editor_mesh_name_cache
{
    std::vector<std::string> patterns; // last-seen project.assets_patterns, to notice a change
    std::unordered_map<std::string, std::filesystem::path> found;
    // known-absent since this steady_clock time; re-walked once stale, so a typo does not
    // re-walk every frame but a file dropped in later is picked up within about a second.
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> missing;
};
constexpr std::chrono::seconds rv_editor_mesh_miss_ttl{ 1 };
rv_editor_mesh_name_cache rv_editor_mesh_names;

// True for a directory the tree walk skips whole: dotfiles and build outputs.
bool rv_editor_mesh_skip_dir(const std::string &name)
{
    return !name.empty() && (name[0] == '.' || name.starts_with("build"));
}

// The project file matching an [assets] pattern whose filename is `name`. Empty with
// *error set when the name has a folder in it or no such file is found.
std::filesystem::path rv_editor_resolve_mesh_name(
    const rv_editor_project &project, const std::string &name, std::string *error)
{
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        set_error(error, name + ": a disc asset name has no folders");
        return {};
    }
    if (rv_editor_mesh_names.patterns != project.assets_patterns) {
        rv_editor_mesh_names = { project.assets_patterns, {}, {} };
    }
    auto cached = rv_editor_mesh_names.found.find(name);
    std::error_code exists_ec;
    if (cached != rv_editor_mesh_names.found.end() && std::filesystem::exists(cached->second, exists_ec)) {
        return cached->second;
    }
    auto missed = rv_editor_mesh_names.missing.find(name);
    if (cached == rv_editor_mesh_names.found.end() && missed != rv_editor_mesh_names.missing.end() &&
        std::chrono::steady_clock::now() - missed->second < rv_editor_mesh_miss_ttl) {
        set_error(error, name + ": no [assets] file by that name");
        return {};
    }
    std::error_code ec;
    constexpr auto opts = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(project.root, opts, ec), end; it != end && !ec;
        it.increment(ec)) {
        std::error_code entry_ec;
        const bool is_dir = it->is_directory(entry_ec);
        if (entry_ec) {
            continue; // this one entry is unreadable or vanished; the walk goes on
        }
        if (is_dir) {
            if (rv_editor_mesh_skip_dir(it->path().filename().string())) {
                it.disable_recursion_pending();
            }
            continue;
        }
        std::error_code rel_ec;
        const std::string rel = std::filesystem::relative(it->path(), project.root, rel_ec).generic_string();
        if (rel_ec || it->path().filename() != name) {
            continue;
        }
        for (const std::string &pattern : project.assets_patterns) {
            if (rv_pdklib::rv_manifest_pattern_matches(pattern, rel)) {
                rv_editor_mesh_names.found[name] = it->path();
                return it->path();
            }
        }
    }
    rv_editor_mesh_names.missing[name] = std::chrono::steady_clock::now();
    set_error(error, name + ": no [assets] file by that name");
    return {};
}

} // namespace

std::vector<rv_editor_tri> rv_editor_object_triangles(
    const rv_editor_scene &scene, const rv_editor_project &project, int index, std::string *error)
{
    const rv_editor_scene_object &o = scene.objects[static_cast<size_t>(index)];
    if (o.kind != "mesh") {
        return {};
    }
    const rv_editor_affine m = rv_editor_scene_world(scene, index);
    if (o.mesh.empty()) {
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
        for (int k = 0; k < 3; ++k) {
            tri.p[k] = rv_editor_affine_point(m, to_vec3(rv_pdklib::rv_obj_position(*mesh, t.corner[k])));
        }
        tris.push_back(tri);
    }
    return tris;
}

void rv_editor_draw_filled(ImDrawList *dl, const rv_editor_view &v, const rv_editor_scene &scene,
    const rv_editor_project &project, const std::string &selected, ImU32 base, ImU32 selected_color)
{
    struct rv_editor_shaded_tri
    {
        ImVec2 s[3];
        double depth;
        float shade;
        bool selected;
    };
    std::vector<rv_editor_shaded_tri> shaded;
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        if (scene.objects[i].kind != "mesh") {
            continue;
        }
        const bool is_selected = scene.objects[i].id == selected;
        for (const rv_editor_tri &tri : rv_editor_object_triangles(scene, project, static_cast<int>(i))) {
            rv_editor_shaded_tri st;
            st.depth = 0.0;
            bool visible = true;
            for (int k = 0; k < 3; ++k) {
                const vec3 view_p = v.to_view(tri.p[k]);
                if (view_p[2] < rv_editor_near) {
                    visible = false;
                    break;
                }
                st.depth += view_p[2];
                st.s[k] = v.to_screen(view_p);
            }
            if (!visible) {
                continue;
            }
            st.depth /= 3.0;
            const vec3 n = norm(cross(sub(tri.p[1], tri.p[0]), sub(tri.p[2], tri.p[0])));
            st.shade = static_cast<float>(std::max(0.15, std::abs(dot(n, mul(v.forward, -1.0)))));
            st.selected = is_selected;
            shaded.push_back(st);
        }
    }
    // ponytail: painter's algorithm, no depth buffer. Intersecting triangles can sort wrong;
    // the upgrade is a real depth test if that ever shows on screen.
    std::sort(shaded.begin(), shaded.end(),
        [](const rv_editor_shaded_tri &a, const rv_editor_shaded_tri &b) { return a.depth > b.depth; });
    for (const rv_editor_shaded_tri &st : shaded) {
        ImVec4 col = ImGui::ColorConvertU32ToFloat4(st.selected ? selected_color : base);
        col.x *= st.shade;
        col.y *= st.shade;
        col.z *= st.shade;
        dl->AddTriangleFilled(st.s[0], st.s[1], st.s[2], ImGui::ColorConvertFloat4ToU32(col));
    }
}

} // namespace rv_editor
