// The Scene viewport's filled pass (Filled and Textured): every mesh, quad and billboard is
// rasterised by the CPU z-buffer into one picture the size of the viewport, drawn as one image.
// Each object is rasterised as it comes, since a part's CPU picture lives only until the next
// rv_editor_object_triangles call.
// Cost: every frame the picture is cleared, rasterised in full and uploaded whole (w * h * 4 bytes),
// and the triangles are rebuilt, as the viewport already did before this pass. Acceptable while the
// picture is one viewport's size and the scene's triangle count stays modest. Ceiling: a large
// viewport on a HiDPI screen, or a heavy scene. ponytail: redraw only when the view, the scene or a
// texture changes; add it when frame time shows this pass.

#include "panes/rv_editor_scene_draw.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "imgui.h"

#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_gte/rv_gte_project.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "panes/rv_editor_scene_glb.hpp"
#include "panes/rv_editor_zbuf.hpp"
#include "scene/rv_editor_scene.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_icons.hpp"

namespace rv_editor
{

namespace
{

// Minimum shade value for unlit triangle surfaces.
constexpr float min_shade_value = 0.15f;
// Maximum value for sRGB colour channel normalization.
constexpr float color_channel_max = 255.0f;
// Normalization threshold: minimum vector length before clamping to zero.
constexpr double normalize_epsilon = 1e-12;
// Bytes per pixel of the picture: RGBA8, as the z-buffer and the SDL texture both are.
constexpr int rgba_channels = 4;
// The near plane in Q20.12 raw: rv_editor_near rounded, as the console's near plane is.
constexpr rv_pdklib::fx near_plane_raw = static_cast<rv_pdklib::fx>(rv_editor_near * rv_pdklib::fx_one + 0.5);

vec3 cross(const vec3 &a, const vec3 &b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

vec3 norm(const vec3 &a)
{
    const double l = std::sqrt(dot(a, a));
    return l > normalize_epsilon ? mul(a, 1.0 / l) : a;
}

// The cull mode of an object as pdkkit's rv_ecs_frame.hpp gives it (two_sided -> none, else back, near line 113),
// which the console's gte_cull_mode in src/rv_pconsole/cg/rv_pccg_sw_put.cpp copies.
std::uint32_t rv_editor_cull_mode(bool two_sided)
{
    return two_sided ? rv_pdklib::gte_cull_none : rv_pdklib::gte_cull_screen_cw;
}

// A viewport's picture: the z-buffer, and the SDL texture its colour is copied to each frame.
// Kept while the size stays the same; its texture is destroyed by rv_editor_scene_pictures_free.
struct rv_editor_scene_target {
    int w = 0;
    int h = 0;
    rv_editor_zbuf buf;
    SDL_Texture *texture = nullptr;
    // A failure is reported once per viewport, not once per frame.
    bool reported = false;
    // Refused triangles are reported once per run of frames with refusals, per viewport.
    bool refusal_reported = false;
};

// One picture per Scene viewport, keyed by its ImGui ID. An entry, and its texture, stays until
// the editor exits: a closed tile's picture is not released (ponytail: release on tile close).
std::unordered_map<ImGuiID, rv_editor_scene_target> rv_editor_targets;

// The failure reason goes to stderr, once per viewport.
void rv_editor_target_report(rv_editor_scene_target &t, int w, int h, const char *reason)
{
    if (t.reported) {
        return;
    }
    t.reported = true;
    std::fprintf(stderr, "3dmppc-editor: scene picture %dx%d: %s\n", w, h, reason);
}

// Makes the picture of `t` w x h on `renderer`, replacing the old one when its size changes.
int rv_editor_target_make(rv_editor_scene_target &t, SDL_Renderer *renderer, int w, int h)
{
    if (t.texture != nullptr && t.w == w && t.h == h) {
        return RV_OK;
    }
    if (t.texture != nullptr) {
        SDL_DestroyTexture(t.texture);
    }
    const bool reported = t.reported;
    t = {};
    t.reported = reported;
    if (rv_editor_zbuf_init(t.buf, w, h) != RV_OK) {
        rv_editor_target_report(t, w, h, "the z-buffer cannot be sized");
        return RV_ERR_INVAL;
    }
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (texture == nullptr) {
        rv_editor_target_report(t, w, h, SDL_GetError());
        return RV_ERR_IO;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    t.w = w;
    t.h = h;
    t.texture = texture;
    return RV_OK;
}

// An object's texture decoded to memory, kept while its file's mtime stays the same.
struct rv_editor_texture_cache_entry {
    std::filesystem::file_time_type mtime;
    bool ok = false;
    rv_editor_cpu_image image;
};

// Keyed by absolute path. Entries are never erased, so an entry's address is stable and a returned
// pointer stays valid; its content is replaced when the file's mtime changes.
std::unordered_map<std::string, rv_editor_texture_cache_entry> rv_editor_texture_cache;

// An object's texture as a picture in memory; null when it has none, does not resolve under
// [textures], or does not load (the reason goes to stderr once per file version).
const rv_editor_cpu_image *rv_editor_object_cpu_texture(const rv_editor_project &project, const std::string &name)
{
    if (name.empty()) {
        return nullptr;
    }
    const std::filesystem::path path = rv_editor_resolve_texture_name(project, name);
    if (path.empty()) {
        return nullptr;
    }
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return nullptr;
    }
    const std::string key = path.string();
    rv_editor_texture_cache_entry &entry = rv_editor_texture_cache[key];
    if (entry.mtime != mtime) {
        entry = {};
        entry.mtime = mtime;
        entry.ok = rv_editor_cpu_image_load(key, entry.image) == RV_OK;
    }
    return entry.ok ? &entry.image : nullptr;
}

// What a triangle piece is drawn with: the viewport's frame, the object's colours, and the
// triangle's shade and picture.
struct rv_editor_piece_style {
    // The viewport's top-left in ImGui units, and picture pixels per ImGui unit.
    ImVec2 origin;
    ImVec2 scale;
    // A plain piece's colour, the selected object's, and the object's tint (0..1).
    ImVec4 base;
    ImVec4 selection;
    ImVec4 tint;
    bool selected = false;
    // The object's cull mode, as rv_editor_cull_mode gives it.
    std::uint32_t cull = rv_pdklib::gte_cull_screen_cw;
    // A .glb's triangle: its base is white and the tint applies, flat too.
    bool from_model = false;
    float shade = 1.0f;
    const rv_editor_cpu_image *texture = nullptr; // null: flat
};

// One triangle piece, its corners already in view space: coloured as the console draws it and
// rasterised into `buf`. Colour: the selection colour when selected (no tint); a plain flat piece
// its base colour, untinted; else white times the object's tint; each times the shade and the
// corner's colour. A flat piece samples an opaque white texel, so the corner colour is the pixel.
void rv_editor_raster_piece(rv_editor_zbuf &buf,
    const rv_editor_view &v,
    const rv_editor_tri &piece,
    const vec3 (&view_p)[tri_vertices],
    const rv_editor_piece_style &st)
{
    const bool flat = st.texture == nullptr;
    const bool plain = flat && !st.from_model;
    const ImVec4 over = st.selected ? st.selection : (plain ? st.base : ImVec4(1, 1, 1, 1));
    const ImVec4 tint = st.selected || plain ? ImVec4(1, 1, 1, 1) : st.tint;
    rv_editor_zbuf_texture picture;
    if (!flat) {
        picture.texels = st.texture->rgba.data();
        picture.width = st.texture->w;
        picture.height = st.texture->h;
    }
    rv_editor_zbuf_triangle zt;
    zt.texture = flat ? nullptr : &picture;
    for (int k = 0; k < tri_vertices; ++k) {
        const ImVec2 s = v.to_screen(view_p[k]);
        const ImVec4 part = st.selected ? ImVec4(1, 1, 1, 1) : piece.color[k];
        const ImVec2 uv = piece.uv[k];
        rv_editor_zbuf_vertex &c = zt.corner[k];
        c.x = (s.x - st.origin.x) * st.scale.x;
        c.y = (s.y - st.origin.y) * st.scale.y;
        c.z = static_cast<float>(view_p[k][2]);
        c.u = flat ? 0.0f : uv.x;
        c.v = flat ? 0.0f : uv.y;
        c.r = over.x * tint.x * st.shade * part.x;
        c.g = over.y * tint.y * st.shade * part.y;
        c.b = over.z * tint.z * st.shade * part.z;
        c.a = over.w;
    }
    // The result is dropped. RV_ERR_INVAL for a corner non-finite or z <= 0 draws nothing, the right
    // outcome for a degenerate piece. RV_ERR_INVAL for a texture without texels or size cannot happen
    // here: the decoder guarantees w, h > 0.
    rv_editor_zbuf_draw(buf, zt);
}

// A scene point as Q20.12, then as view space through the camera's view matrix. RV_ERR_INVAL when either leaves int32.
int rv_editor_view_fx_point(const rv_editor_view &v, const vec3 &p, rv_pdklib::fx_vec3 &out)
{
    std::int32_t scene[tri_vertices] = {};
    for (int k = 0; k < tri_vertices; ++k) {
        if (rv_pdklib::fx_from_double(p[k], scene[k]) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    std::int32_t view[tri_vertices] = {};
    if (rv_pdklib::rv_gte_point(v.camera.view, scene, view) != RV_OK) {
        return RV_ERR_INVAL;
    }
    out = { view[0], view[1], view[2] };
    return RV_OK;
}

// The triangle's corners in view space as Q20.12, with its uv and colour. RV_ERR_INVAL when a corner leaves int32.
int rv_editor_view_fx_tri(const rv_editor_view &v, const rv_editor_tri &tri, rv_editor_fx_tri &out)
{
    for (int k = 0; k < tri_vertices; ++k) {
        if (rv_editor_view_fx_point(v, tri.p[k], out.v[k].p) != RV_OK) {
            return RV_ERR_INVAL;
        }
        out.v[k].uv = tri.uv[k];
        out.v[k].color = tri.color[k];
    }
    return RV_OK;
}

// A cut piece as the double triangle rv_editor_raster_piece takes: its uv and colour, and its corners in view-space
// metres, raw / units (exact). The scene points are not carried: the rasteriser reads only the view points.
void rv_editor_piece_metres(const rv_editor_fx_tri &piece, rv_editor_tri &tri, vec3 (&view_p)[tri_vertices])
{
    for (int k = 0; k < tri_vertices; ++k) {
        const rv_pdklib::fx_vec3 &p = piece.v[k].p;
        view_p[k] = { static_cast<double>(p.x) / rv_editor_gte_units_per_metre,
            static_cast<double>(p.y) / rv_editor_gte_units_per_metre,
            static_cast<double>(p.z) / rv_editor_gte_units_per_metre };
        tri.uv[k] = piece.v[k].uv;
        tri.color[k] = piece.v[k].color;
    }
}

// Whether the console drops a cut piece under `cull`: its corners projected as rv_gte_project does, with the
// view's h and the centre at the origin. The signed area is unchanged by a centre shift only while the corners
// stay inside gte_screen_limit, where rv_gte_clamp_screen does not alter them. Zero area is dropped in every
// mode, as the console does. RV_ERR_INVAL when a corner does not project: the near cut leaves every corner at
// z >= the near plane > 0, and rv_gte_project fails only at z <= 0, so this error means a broken cut.
int rv_editor_piece_culled(const rv_editor_view &v, const rv_editor_fx_tri &piece, std::uint32_t cull, bool &culled)
{
    rv_pdklib::rv_gte_screen screen[tri_vertices] = {};
    for (int k = 0; k < tri_vertices; ++k) {
        if (rv_pdklib::rv_gte_project(piece.v[k].p, { v.camera.h, 0, 0 }, screen[k]) != RV_OK) {
            return RV_ERR_INVAL;
        }
    }
    const bool zero_area = rv_pdklib::rv_gte_culled(screen[0], screen[1], screen[2], rv_pdklib::gte_cull_screen_cw) &&
        rv_pdklib::rv_gte_culled(screen[0], screen[1], screen[2], rv_pdklib::gte_cull_screen_ccw);
    culled = zero_area || rv_pdklib::rv_gte_culled(screen[0], screen[1], screen[2], cull);
    return RV_OK;
}

// One triangle: corners to view space, cut at the near plane, each piece culled and rasterised. RV_ERR_INVAL when a
// corner or cut point leaves int32, or a piece does not project; the triangle is then not drawn further.
int rv_editor_raster_tri(rv_editor_zbuf &buf,
    const rv_editor_view &v,
    const rv_editor_tri &tri,
    const rv_editor_piece_style &st)
{
    rv_editor_fx_tri source{};
    if (rv_editor_view_fx_tri(v, tri, source) != RV_OK) {
        return RV_ERR_INVAL;
    }
    rv_editor_fx_tri cut[rv_editor_fx_max_pieces] = {};
    int count = 0;
    if (rv_editor_clip_near_fx(source, near_plane_raw, cut, count) != RV_OK) {
        return RV_ERR_INVAL;
    }
    for (int i = 0; i < count; ++i) {
        bool culled = false;
        if (rv_editor_piece_culled(v, cut[i], st.cull, culled) != RV_OK) {
            return RV_ERR_INVAL;
        }
        if (culled) {
            continue;
        }
        rv_editor_tri piece{};
        vec3 view_p[tri_vertices] = {};
        rv_editor_piece_metres(cut[i], piece, view_p);
        rv_editor_raster_piece(buf, v, piece, view_p, st);
    }
    return RV_OK;
}

} // namespace

void rv_editor_draw_filled(ImDrawList *dl,
    const rv_editor_view &v,
    const rv_editor_scene_doc &doc,
    const rv_editor_project &project,
    const std::string &selected,
    ImU32 base,
    ImU32 selected_color,
    SDL_Renderer *renderer,
    ImVec2 origin,
    ImVec2 size,
    bool textured)
{
    // The picture is in framebuffer pixels, so a HiDPI UI scale keeps it sharp.
    const ImVec2 fb = ImGui::GetIO().DisplayFramebufferScale;
    const int w = std::max(1, static_cast<int>(std::lround(size.x * fb.x)));
    const int h = std::max(1, static_cast<int>(std::lround(size.y * fb.y)));
    // The picture is keyed by the ImGui ID of the viewport's tile: each Scene tile draws its own.
    rv_editor_scene_target &t = rv_editor_targets[ImGui::GetID("scene picture")];
    if (rv_editor_target_make(t, renderer, w, h) != RV_OK) {
        return; // the reason is reported in rv_editor_target_make
    }
    rv_editor_zbuf_clear(t.buf);
    int refused = 0;
    rv_editor_piece_style st;
    st.origin = origin;
    st.scale = ImVec2(static_cast<float>(w) / size.x, static_cast<float>(h) / size.y);
    st.base = ImGui::ColorConvertU32ToFloat4(base);
    st.selection = ImGui::ColorConvertU32ToFloat4(selected_color);
    const rv_editor_scene &scene = doc.scene;
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const rv_editor_scene_object &o = scene.objects[i];
        const std::string kind = rv_editor_object_get_kind(o);
        if (kind != kind_mesh && kind != kind_quad && kind != kind_billboard) {
            continue; // volumes, cameras, groups and other kinds are not filled
        }
        if (rv_editor_scene_visibility(doc, o) != rv_editor_visibility::shown) {
            continue; // an outline is drawn as edges, an absent object not at all
        }
        const std::string texture = rv_editor_object_get_texture(o);
        const rv_editor_cpu_image *tex = textured ? rv_editor_object_cpu_texture(project, texture) : nullptr;
        const rv_editor_tint tint = rv_editor_object_get_tint(o);
        st.selected = o.id == selected;
        st.cull = rv_editor_cull_mode(rv_editor_scene_has_component(o, component_two_sided));
        st.tint = { tint[0] / color_channel_max, tint[1] / color_channel_max, tint[2] / color_channel_max, 1.0f };
        const std::vector<rv_editor_tri> tris = rv_editor_object_triangles(scene, project, static_cast<int>(i));
        for (const rv_editor_tri &tri : tris) {
            // Shade by the source triangle's normal; its cut pieces share it.
            const vec3 n = norm(cross(sub(tri.p[1], tri.p[0]), sub(tri.p[2], tri.p[0])));
            const double facing = std::abs(dot(n, mul(v.forward, -1.0)));
            st.shade = static_cast<float>(std::max(static_cast<double>(min_shade_value), facing));
            // A part's own picture wins. A .glb part without one stays flat, as in the console; other meshes keep the object's.
            const rv_editor_cpu_image *piece_tex = tri.cpu_tex != nullptr ? tri.cpu_tex : (tri.from_model ? nullptr : tex);
            st.texture = textured ? piece_tex : nullptr;
            st.from_model = tri.from_model;
            if (rv_editor_raster_tri(t.buf, v, tri, st) != RV_OK) {
                ++refused;
            }
        }
    }
    // Reported once per run of frames with refusals: the flag clears on a frame without any.
    if (refused == 0) {
        t.refusal_reported = false;
    } else if (!t.refusal_reported) {
        t.refusal_reported = true;
        RV_LOG_WARN(scene_log_tag,
            "{}",
            rv_editor_text_format("project.filled_triangles_refused", std::make_format_args(refused)));
    }
    SDL_UpdateTexture(t.texture, nullptr, t.buf.colour.data(), w * rgba_channels);
    const ImTextureID id = static_cast<ImTextureID>(reinterpret_cast<intptr_t>(t.texture));
    dl->AddImage(ImTextureRef(id), origin, ImVec2(origin.x + size.x, origin.y + size.y));
}

void rv_editor_scene_pictures_free()
{
    for (auto &entry : rv_editor_targets) {
        if (entry.second.texture != nullptr) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    rv_editor_targets.clear();
}

} // namespace rv_editor
