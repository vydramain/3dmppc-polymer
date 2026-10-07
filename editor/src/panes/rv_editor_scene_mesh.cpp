// A mesh object's real geometry: its .obj file, cached and transformed into scene
// space, and the filled-polygon draw mode built on top of it.

#include "panes/rv_editor_scene_draw.hpp"

#include <algorithm>
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

#include "panes/rv_editor_asset_preview.hpp"
#include "scene/rv_editor_scene_edit.hpp"

namespace rv_editor
{

namespace
{

// Half-extent of placeholder geometric primitives (unit cube and quad, local coordinates).
constexpr double unit_half_extent = 0.5;
// Minimum shade value for unlit triangle surfaces.
constexpr float min_shade_value = 0.15f;
// Maximum value for sRGB colour channel normalization.
constexpr float color_channel_max = 255.0f;
// Directory name pattern to exclude from asset search.
constexpr std::string_view skip_build_dir_name = "build";
// Normalization threshold: minimum vector length before clamping to zero.
constexpr double normalize_epsilon = 1e-12;
// Vertex count for triangles: used for depth averaging and geometry creation.
constexpr int tri_vertices = 3;
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
// Number of vertices per triangle for depth averaging (derived from tri_vertices).
constexpr double triangle_vertex_count = static_cast<double>(tri_vertices);

vec3 cross(const vec3 &a, const vec3 &b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

vec3 norm(const vec3 &a)
{
    const double l = std::sqrt(dot(a, a));
    return l > normalize_epsilon ? mul(a, 1.0 / l) : a;
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
struct rv_editor_name_cache {
    std::vector<std::string> patterns; // last-seen manifest patterns, to notice a change
    std::unordered_map<std::string, std::filesystem::path> found;
    // known-absent since this steady_clock time; re-walked once stale, so a typo does not
    // re-walk every frame but a file dropped in later is picked up within about a second.
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> missing;
};
constexpr std::chrono::seconds rv_editor_mesh_miss_ttl{ 1 };
rv_editor_name_cache rv_editor_mesh_names;
rv_editor_name_cache rv_editor_texture_names;

// True for a directory the tree walk skips whole: dotfiles and build outputs.
bool rv_editor_mesh_skip_dir(const std::string &name)
{
    return !name.empty() && (name[0] == '.' || name.starts_with(skip_build_dir_name));
}

// The project file under `patterns` for which `match` holds, cached in `cache` under `key`.
// Shared by the mesh (match: exact filename) and texture (match: same stem) resolvers below.
std::filesystem::path rv_editor_walk_project(const rv_editor_project &project,
    rv_editor_name_cache &cache,
    const std::vector<std::string> &patterns,
    const std::string &key,
    const std::function<bool(const std::filesystem::path &)> &match)
{
    if (cache.patterns != patterns) {
        cache = { patterns, {}, {} };
    }
    auto cached = cache.found.find(key);
    std::error_code exists_ec;
    if (cached != cache.found.end() && std::filesystem::exists(cached->second, exists_ec)) {
        return cached->second;
    }
    auto missed = cache.missing.find(key);
    if (cached == cache.found.end() && missed != cache.missing.end() &&
        std::chrono::steady_clock::now() - missed->second < rv_editor_mesh_miss_ttl) {
        return {};
    }
    std::error_code ec;
    constexpr auto opts = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(project.root, opts, ec), end; it != end && !ec; it.increment(ec)) {
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
        if (!match(it->path())) {
            continue;
        }
        std::error_code rel_ec;
        const std::string rel = std::filesystem::relative(it->path(), project.root, rel_ec).generic_string();
        if (rel_ec) {
            continue;
        }
        for (const std::string &pattern : patterns) {
            if (rv_pdklib::rv_manifest_pattern_matches(pattern, rel)) {
                cache.found[key] = it->path();
                return it->path();
            }
        }
    }
    cache.missing[key] = std::chrono::steady_clock::now();
    return {};
}

// The project file matching an [assets] pattern whose filename is `name`. Empty with
// *error set when the name has a folder in it or no such file is found.
std::filesystem::path rv_editor_resolve_mesh_name(const rv_editor_project &project, const std::string &name, std::string *error)
{
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        set_error(error, name + ": a disc asset name has no folders");
        return {};
    }
    const std::filesystem::path found = rv_editor_walk_project(project,
        rv_editor_mesh_names,
        project.assets_patterns,
        name,
        [&](const std::filesystem::path &p) {
            return p.filename() == name;
        });
    if (found.empty()) {
        set_error(error, name + ": no [assets] file by that name");
    }
    return found;
}

// A scene's `texture` is a flat disc texture name (mppcburner: the source's stem plus
// ".mppctex"), resolved to the project file under [textures] whose stem matches.
std::filesystem::path rv_editor_resolve_texture_name(const rv_editor_project &project, const std::string &name)
{
    const std::string stem = std::filesystem::path(name).stem().string();
    return rv_editor_walk_project(project,
        rv_editor_texture_names,
        project.textures_patterns,
        stem,
        [&](const std::filesystem::path &p) {
            return p.stem().string() == stem;
        });
}

// A mesh object's texture, loaded on `renderer`; an empty icon when it has none, does not
// resolve under [textures], or fails to load as a picture.
rv_editor_icon
rv_editor_object_texture(SDL_Renderer *renderer, const rv_editor_project &project, const rv_editor_scene_object &o)
{
    if (renderer == nullptr || o.texture.empty()) {
        return {};
    }
    const std::filesystem::path path = rv_editor_resolve_texture_name(project, o.texture);
    if (path.empty()) {
        return {};
    }
    std::error_code rel_ec;
    rv_editor_asset asset;
    asset.path = path;
    asset.rel = std::filesystem::relative(path, project.root, rel_ec).generic_string();
    return rv_editor_asset_picture(renderer, asset);
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

// A billboard's two triangles: a card of world size scale.xy (a scaled parent included)
// centred on the object's world position, built from the view's right/up (not the object's
// own rotation) so it always faces the camera.
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

} // namespace

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

void rv_editor_draw_filled(ImDrawList *dl,
    const rv_editor_view &v,
    const rv_editor_scene &scene,
    const rv_editor_project &project,
    const std::string &selected,
    ImU32 base,
    ImU32 selected_color,
    SDL_Renderer *renderer)
{
    struct rv_editor_shaded_tri {
        ImVec2 s[3];
        ImVec2 uv[3];
        double depth;
        float shade;
        bool selected;
        ImVec4 tint;     // the object's tint/255, 0..1; identity (1,1,1) for a mesh's default
        ImTextureID tex; // 0: flat AddTriangleFilled; else textured via Prim*
    };
    std::vector<rv_editor_shaded_tri> shaded;
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const rv_editor_scene_object &o = scene.objects[i];
        if (o.kind != kind_mesh && o.kind != kind_quad && o.kind != kind_billboard) {
            continue; // volumes, cameras, groups and other kinds are not filled
        }
        const bool is_selected = o.id == selected;
        const rv_editor_icon tex = rv_editor_object_texture(renderer, project, o);
        // quad/billboard uv is a pixel rect, normalized here once the texture's size is known;
        // a mesh's is already 0..1 from rv_editor_object_triangles.
        const bool pixel_uv = o.kind != kind_mesh && tex.id != ImTextureID{} && tex.w > 0 && tex.h > 0;
        std::vector<rv_editor_tri> tris;
        if (o.kind == kind_billboard) {
            const rv_editor_affine world = rv_editor_scene_world(scene, static_cast<int>(i));
            tris = rv_editor_billboard_triangles(v, world, o);
        } else {
            tris = rv_editor_object_triangles(scene, project, static_cast<int>(i));
        }
        for (const rv_editor_tri &tri : tris) {
            rv_editor_shaded_tri st;
            st.depth = 0.0;
            bool visible = true;
            for (int k = 0; k < tri_vertices; ++k) {
                const vec3 view_p = v.to_view(tri.p[k]);
                if (view_p[2] < rv_editor_near) {
                    visible = false;
                    break;
                }
                st.depth += view_p[2];
                st.s[k] = v.to_screen(view_p);
                st.uv[k] = pixel_uv ? ImVec2(tri.uv[k].x / static_cast<float>(tex.w), tri.uv[k].y / static_cast<float>(tex.h)) :
                                      tri.uv[k];
            }
            if (!visible) {
                continue;
            }
            st.depth /= triangle_vertex_count;
            const vec3 n = norm(cross(sub(tri.p[1], tri.p[0]), sub(tri.p[2], tri.p[0])));
            st.shade =
                static_cast<float>(std::max(static_cast<double>(min_shade_value), std::abs(dot(n, mul(v.forward, -1.0)))));
            st.selected = is_selected;
            st.tint = { o.tint[0] / color_channel_max, o.tint[1] / color_channel_max, o.tint[2] / color_channel_max, 1.0f };
            st.tex = tex.id;
            shaded.push_back(st);
        }
    }
    // Painter's algorithm, no depth buffer. Intersecting triangles can sort wrong;
    // the upgrade is a real depth test if that ever shows on screen.
    std::sort(shaded.begin(), shaded.end(), [](const rv_editor_shaded_tri &a, const rv_editor_shaded_tri &b) {
        return a.depth > b.depth;
    });
    for (const rv_editor_shaded_tri &st : shaded) {
        if (st.tex == ImTextureID{}) {
            // Flat: the base or selection colour times shade; the object's tint is a texture
            // modulation (quad/billboard) and does not apply to a flat fill.
            ImVec4 col = ImGui::ColorConvertU32ToFloat4(st.selected ? selected_color : base);
            col.x *= st.shade;
            col.y *= st.shade;
            col.z *= st.shade;
            dl->AddTriangleFilled(st.s[0], st.s[1], st.s[2], ImGui::ColorConvertFloat4ToU32(col));
            continue;
        }
        // Textured: the picture multiplied by the flat shade and, unselected, the object's
        // tint (a mesh's default tint is identity, so this leaves mesh unchanged); selected
        // keeps its selection colour instead of grey or tint. A push/pop per triangle keeps
        // texture changes correct under the global sort.
        const ImVec4 sel_tint = st.selected ? ImGui::ColorConvertU32ToFloat4(selected_color) : ImVec4(1, 1, 1, 1);
        const ImVec4 obj_tint = st.selected ? ImVec4(1, 1, 1, 1) : st.tint;
        const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(sel_tint.x * obj_tint.x * st.shade,
            sel_tint.y * obj_tint.y * st.shade,
            sel_tint.z * obj_tint.z * st.shade,
            1.0f));
        dl->PushTexture(st.tex);
        dl->PrimReserve(tri_vertices, tri_vertices);
        dl->PrimVtx(st.s[0], st.uv[0], col);
        dl->PrimVtx(st.s[1], st.uv[1], col);
        dl->PrimVtx(st.s[2], st.uv[2], col);
        dl->PopTexture();
    }
}

} // namespace rv_editor
