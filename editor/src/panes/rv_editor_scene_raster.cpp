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
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "imgui.h"

#include "pdk/cv/rv_primitives.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_gte/rv_gte_project.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "panes/rv_editor_scene_glb.hpp"
#include "panes/rv_editor_zbuf.hpp"
#include "scene/rv_editor_scene.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_icons.hpp"

namespace rv_editor
{

namespace
{

// Maximum value for sRGB colour channel normalization.
constexpr float color_channel_max = 255.0f;
// Bytes per pixel of the picture: RGBA8, as the z-buffer and the SDL texture both are.
constexpr int rgba_channels = 4;
// The shade as a raw 0..255 factor; rv_editor_shade_raw computes it as an integer from the normal.
constexpr int RV_EDITOR_SHADE_SCALE = 255;
// The lowest shade, raw: 0.15 * 255 = 38.25, rounded half up to 38.
constexpr int RV_EDITOR_SHADE_MIN_RAW = 38;
// Shift bounds: edge components below 2^20 (cross product below 2^41); normal below 2^30 (squares sum below 2^62).
constexpr int RV_EDITOR_SHADE_EDGE_BITS = 20;
constexpr int RV_EDITOR_SHADE_NORMAL_BITS = 30;
// A colour channel as a raw 0..255 factor (u8 at the source). The vertex colour is four such factors, so its
// divisor is 255 cubed; the console's scale_channel divides three factors by 255 squared.
constexpr int64_t RV_EDITOR_COLOUR_SCALE = 255;
constexpr int64_t RV_EDITOR_COLOUR_DIVISOR = RV_EDITOR_COLOUR_SCALE * RV_EDITOR_COLOUR_SCALE * RV_EDITOR_COLOUR_SCALE;

// The floor shift that brings `largest` down to `bits` bits; one shift for a whole set of components.
int shift_for(int64_t largest, int bits)
{
    return std::max(0, static_cast<int>(std::bit_width(static_cast<uint64_t>(largest))) - bits);
}

// The triangle's shade, raw 0..255: |n . forward| / |n|, rounded half up, not below RV_EDITOR_SHADE_MIN_RAW. No q:
// the lowest shade. A zero normal is zero area, which the z-buffer draws nothing for.
int rv_editor_shade_raw(const rv_editor_tri &tri, const rv_editor_view &v)
{
    if (!tri.q_valid) {
        return RV_EDITOR_SHADE_MIN_RAW;
    }
    // Edges are Q20.12 differences, up to 2^32 per component. Each edge has its own shift: the cross product then
    // scales by a positive 2^-(s1+s2), which leaves its direction, |n| and the n.f ratio unchanged except for truncation.
    const rv_pdklib::fx_vec3 &p0 = tri.q[0];
    int64_t e1[3] = { int64_t{ tri.q[1].x } - p0.x, int64_t{ tri.q[1].y } - p0.y, int64_t{ tri.q[1].z } - p0.z };
    int64_t e2[3] = { int64_t{ tri.q[2].x } - p0.x, int64_t{ tri.q[2].y } - p0.y, int64_t{ tri.q[2].z } - p0.z };
    const int s1 = shift_for(std::max({ std::abs(e1[0]), std::abs(e1[1]), std::abs(e1[2]) }), RV_EDITOR_SHADE_EDGE_BITS);
    const int s2 = shift_for(std::max({ std::abs(e2[0]), std::abs(e2[1]), std::abs(e2[2]) }), RV_EDITOR_SHADE_EDGE_BITS);
    for (int k = 0; k < 3; ++k) {
        e1[k] >>= s1;
        e2[k] >>= s2;
    }
    int64_t n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
    const int64_t largest = std::max({ std::abs(n[0]), std::abs(n[1]), std::abs(n[2]) });
    const int normal_shift = shift_for(largest, RV_EDITOR_SHADE_NORMAL_BITS);
    int64_t length_sq = 0;
    for (int k = 0; k < 3; ++k) {
        n[k] >>= normal_shift;
        length_sq += n[k] * n[k];
    }
    const uint64_t length = rv_pdklib::fx_isqrt(static_cast<uint64_t>(length_sq));
    // The view's forward axis: the third row of the 4.12 view matrix. A zero normal gives dot 0, the lowest shade.
    const std::int16_t *forward = &v.camera.view.r[6];
    const int64_t dot = n[0] * forward[0] + n[1] * forward[1] + n[2] * forward[2];
    const int64_t den = std::max<int64_t>(static_cast<int64_t>(length), 1) * rv_pdklib::fx_one;
    const int64_t facing = (std::abs(dot) * RV_EDITOR_SHADE_SCALE + den / 2) / den;
    return static_cast<int>(std::clamp<int64_t>(facing, RV_EDITOR_SHADE_MIN_RAW, RV_EDITOR_SHADE_SCALE));
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
    // A plain piece's colour, the selected object's, and the object's tint (0..1).
    ImVec4 base;
    ImVec4 selection;
    ImVec4 tint;
    bool selected = false;
    // The object's cull mode, as rv_editor_cull_mode gives it.
    std::uint32_t cull = rv_pdklib::gte_cull_screen_cw;
    // The buffer's projection: its focal length and centre, in buffer pixels.
    rv_pdklib::rv_gte_viewport screen{};
    // A .glb's triangle: its base is white and the tint applies, flat too.
    bool from_model = false;
    // The shade, raw 0..255 (RV_EDITOR_SHADE_SCALE).
    int shade = RV_EDITOR_SHADE_SCALE;
    const rv_editor_cpu_image *texture = nullptr; // null: flat
};

// Logs a refusal's cause once: `reported` is the refusing site's own flag, so each site reports once.
// Logged once per process; the per-frame count in rv_editor_draw_filled shows that it recurs.
void rv_editor_refuse_once(bool &reported, const char *what)
{
    if (reported) {
        return;
    }
    reported = true;
    std::fprintf(stderr, "3dmppc-editor: filled triangle refused: %s\n", what);
}

// A unit colour as its raw 0..255 value: lround undoes the float division. Exact for over, tint and base (ImGui
// colours from u8). A model part is already a byte product rounded in glb.cpp, so as a factor it can differ by up to
// 1 LSB from the exact four-factor rule.
int64_t raw_unit(float unit)
{
    return std::lround(unit * static_cast<float>(RV_EDITOR_COLOUR_SCALE));
}

// One vertex colour channel: over, tint, shade and part as raw factors, rounded half up to a byte. The console's
// scale_channel (src/rv_pconsole/cg/rv_pccg_sw_put.cpp) is the same rule with three factors; the shade is the fourth.
uint8_t colour_channel(float over, float tint, int shade, float part)
{
    const int64_t product = raw_unit(over) * raw_unit(tint) * shade * raw_unit(part);
    return static_cast<uint8_t>((product + RV_EDITOR_COLOUR_DIVISOR / 2) / RV_EDITOR_COLOUR_DIVISOR);
}

// Whether a uv component is a finite int16 4.12 value once scaled to raw (the model's range).
bool uv_fits(float component)
{
    const float raw = component * static_cast<float>(RV_EDITOR_ZBUF_UV_ONE);
    return std::isfinite(raw) && raw >= static_cast<float>(std::numeric_limits<int16_t>::min()) &&
        raw <= static_cast<float>(std::numeric_limits<int16_t>::max());
}

// One cut piece, its corners in view space as Q20.12: projected onto the buffer's pixels as rv_gte_project
// does, coloured as the console draws it and rasterised into `buf`. Colour: the selection colour when
// selected (no tint); a plain flat piece its base colour, untinted; else white times the object's tint;
// each times the shade and the corner's colour. A flat piece samples an opaque white texel, so the
// corner colour is the pixel. The piece's ordering key is the console's (rv_gte_depth_key, as make_primitive
// in src/rv_pconsole/cg/rv_pccg_sw_put.cpp), over the editor's near and far planes and the default window
// RV_DEPTH_MIN..RV_DEPTH_MAX: the editor has no disc put, so it draws as a put with the default range does.
// RV_ERR_INVAL when a corner does not project, the key cannot be built, or the z-buffer rejects its input;
// the caller counts it as refused.
int rv_editor_raster_piece(rv_editor_zbuf &buf, const rv_editor_fx_tri &piece, const rv_editor_piece_style &st)
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
    const rv_pdklib::fx depth_z[tri_vertices] = { piece.v[0].p.z, piece.v[1].p.z, piece.v[2].p.z };
    const rv_pdklib::rv_gte_depth_range range = { rv_editor_near_fx, rv_editor_camera_far_fx, RV_DEPTH_MIN, RV_DEPTH_MAX };
    if (rv_pdklib::rv_gte_depth_key(depth_z, tri_vertices, range, zt.depth) != RV_OK) {
        // Once per process, see rv_editor_refuse_once.
        static bool reported = false;
        rv_editor_refuse_once(reported, "a piece's depth key cannot be built (far <= near, or the span is too large)");
        return RV_ERR_INVAL;
    }
    for (int k = 0; k < tri_vertices; ++k) {
        rv_pdklib::rv_gte_screen s{};
        if (rv_pdklib::rv_gte_project(piece.v[k].p, st.screen, s) != RV_OK) {
            // Once per process, see rv_editor_refuse_once.
            static bool reported = false;
            rv_editor_refuse_once(reported, "a piece's corner does not project onto the buffer (z <= 0)");
            return RV_ERR_INVAL;
        }
        const ImVec4 part = st.selected ? ImVec4(1, 1, 1, 1) : piece.v[k].color;
        const ImVec2 uv = piece.v[k].uv;
        // A textured uv must be a finite int16 4.12 value; a flat piece never reads it.
        if (!flat && (!uv_fits(uv.x) || !uv_fits(uv.y))) {
            // Once per process, see rv_editor_refuse_once.
            static bool reported = false;
            rv_editor_refuse_once(reported, "a textured piece's uv is not finite or outside the int16 4.12 range");
            return RV_ERR_INVAL;
        }
        rv_editor_zbuf_vertex &c = zt.corner[k];
        c.x = s.x;
        c.y = s.y;
        // Exact for model uv: raw / 4096 in float, so the rounding only undoes that division. The cube's uv are the
        // unit square's corners, 0 or 1 (quad_uv_corners in rv_editor_scene_mesh.cpp), raw 0 or 4096.
        c.u = flat ? 0 : static_cast<int32_t>(std::lround(uv.x * static_cast<float>(RV_EDITOR_ZBUF_UV_ONE)));
        c.v = flat ? 0 : static_cast<int32_t>(std::lround(uv.y * static_cast<float>(RV_EDITOR_ZBUF_UV_ONE)));
        c.r = colour_channel(over.x, tint.x, st.shade, part.x);
        c.g = colour_channel(over.y, tint.y, st.shade, part.y);
        c.b = colour_channel(over.z, tint.z, st.shade, part.z);
        c.a = static_cast<uint8_t>(raw_unit(over.w));
    }
    if (rv_editor_zbuf_draw(buf, zt) != RV_OK) {
        // Once per process, see rv_editor_refuse_once; rv_editor_zbuf_draw has logged the cause.
        static bool reported = false;
        rv_editor_refuse_once(reported, "the z-buffer rejected a piece's input (its own log names the check)");
        return RV_ERR_INVAL;
    }
    return RV_OK;
}

// A Q20.12 world point to view space through the camera's view matrix. RV_ERR_INVAL when the view point leaves int32.
int rv_editor_view_fx_point(const rv_editor_view &v, const rv_pdklib::fx_vec3 &scene, rv_pdklib::fx_vec3 &out)
{
    const std::int32_t in[tri_vertices] = { scene.x, scene.y, scene.z };
    std::int32_t view[tri_vertices] = {};
    if (rv_pdklib::rv_gte_point(v.camera.view, in, view) != RV_OK) {
        return RV_ERR_INVAL;
    }
    out = { view[0], view[1], view[2] };
    return RV_OK;
}

// The triangle's corners in view space as Q20.12, with its uv and colour. RV_ERR_INVAL when the triangle has no q
// (q_valid is false) or a corner leaves int32.
int rv_editor_view_fx_tri(const rv_editor_view &v, const rv_editor_tri &tri, rv_editor_fx_tri &out)
{
    if (!tri.q_valid) {
        return RV_ERR_INVAL;
    }
    for (int k = 0; k < tri_vertices; ++k) {
        if (rv_editor_view_fx_point(v, tri.q[k], out.v[k].p) != RV_OK) {
            return RV_ERR_INVAL;
        }
        out.v[k].uv = tri.uv[k];
        out.v[k].color = tri.color[k];
    }
    return RV_OK;
}

// Whether the console drops a cut piece under `cull`: its corners projected onto the buffer as the raster
// projects them (vp), so the cull and the drawn piece agree at any scale. The signed area is unchanged by the
// projection's centre only while the corners stay inside gte_screen_limit, where rv_gte_clamp_screen does not
// alter them (rv_editor_screen_culled decides).
// RV_ERR_INVAL when a corner does not project: the near cut leaves every corner at z >= the near plane > 0,
// and rv_gte_project fails only at z <= 0, so this error means a broken cut.
int rv_editor_piece_culled(rv_pdklib::rv_gte_viewport vp, const rv_editor_fx_tri &piece, std::uint32_t cull, bool &culled)
{
    rv_pdklib::rv_gte_screen screen[tri_vertices] = {};
    for (int k = 0; k < tri_vertices; ++k) {
        if (rv_pdklib::rv_gte_project(piece.v[k].p, vp, screen[k]) != RV_OK) {
            // Once per process, see rv_editor_refuse_once.
            static bool reported = false;
            rv_editor_refuse_once(reported, "a cut piece's corner does not project for the cull (z <= 0)");
            return RV_ERR_INVAL;
        }
    }
    culled = rv_editor_screen_culled(screen, cull);
    return RV_OK;
}

// One triangle: corners to view space, cut at the near plane, each piece culled and rasterised. RV_ERR_INVAL when a
// corner or cut point leaves int32. A refused piece does not stop its siblings: every piece is tried, and the
// first failure is the triangle's result.
int rv_editor_raster_tri(rv_editor_zbuf &buf,
    const rv_editor_view &v,
    const rv_editor_tri &tri,
    const rv_editor_piece_style &st)
{
    rv_editor_fx_tri source{};
    if (rv_editor_view_fx_tri(v, tri, source) != RV_OK) {
        // Once per process, see rv_editor_refuse_once.
        static bool reported = false;
        rv_editor_refuse_once(reported, "a triangle has no Q20.12 corners, or a corner leaves int32 in view space");
        return RV_ERR_INVAL;
    }
    rv_editor_fx_tri cut[rv_editor_fx_max_pieces] = {};
    int count = 0;
    if (rv_editor_clip_near_fx(source, rv_editor_near_fx, cut, count) != RV_OK) {
        // Once per process, see rv_editor_refuse_once.
        static bool reported = false;
        rv_editor_refuse_once(reported, "the near-plane cut failed");
        return RV_ERR_INVAL;
    }
    int result = RV_OK;
    for (int i = 0; i < count; ++i) {
        bool culled = false;
        const int cull_rc = rv_editor_piece_culled(st.screen, cut[i], st.cull, culled);
        if (cull_rc != RV_OK && result == RV_OK) {
            result = cull_rc;
        }
        if (cull_rc != RV_OK || culled) {
            continue;
        }
        const int draw_rc = rv_editor_raster_piece(buf, cut[i], st);
        if (draw_rc != RV_OK && result == RV_OK) {
            result = draw_rc;
        }
    }
    return result;
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
    // The buffer's own projection: the viewport's field at the buffer's height, centred at (w / 2, h / 2)
    // in whole pixels. For an odd size that centre is half a pixel left of, and up from, the true one (D4).
    rv_pdklib::rv_gte_viewport screen{};
    if (rv_pdklib::rv_gte_camera_focal(rv_editor_camera_fov_fx, h, screen.h) != RV_OK) {
        rv_editor_target_report(t, w, h, "the focal length cannot be built");
        return;
    }
    screen.cx = w / 2;
    screen.cy = h / 2;
    rv_editor_zbuf_clear(t.buf);
    int refused = 0;
    rv_editor_piece_style st;
    st.screen = screen;
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
            // Shade by the source triangle's normal, from its Q20.12 corners; its cut pieces share it.
            st.shade = rv_editor_shade_raw(tri, v);
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
