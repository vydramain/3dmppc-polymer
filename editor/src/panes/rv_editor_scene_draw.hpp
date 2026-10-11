#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "project/rv_editor_project.hpp"
#include "scene/rv_editor_scene_edit.hpp"

struct SDL_Renderer;

namespace rv_editor
{

struct rv_editor_app;
struct rv_editor_scene_camera;
struct rv_editor_theme;
struct rv_editor_view;

// The Scene viewport's tool drag: whether one runs, its axis, and the mouse where it began.
struct rv_editor_gizmo_drag {
    bool dragging = false;
    int axis = -1;
    std::array<float, 2> from{};
    // The object's values before the drag, which Escape takes back.
    rv_editor_vec3 before_position{};
    rv_editor_vec3 before_rotation{};
    rv_editor_vec3 before_scale{};
};

// Scene viewport tools: toolbar UI, object manipulation, view framing.
void rv_editor_scene_toolbar(rv_editor_app &app, rv_editor_scene_camera &cam, const rv_editor_theme &theme);
void rv_editor_manipulate(rv_editor_app &app,
    const rv_editor_scene_camera &cam,
    const rv_editor_view &v,
    const rv_editor_gizmo_drag &drag);
void rv_editor_scene_frame(rv_editor_app &app, rv_editor_scene_camera &cam, bool all);

// The Scene viewport's geometry (editor/src/panes/rv_editor_scene_viewport.cpp):
// the editor camera's frame, how scene points land on screen, what each object
// draws as, and what the mouse is over.

using vec3 = std::array<double, 3>;

vec3 sub(const vec3 &a, const vec3 &b);
vec3 add(const vec3 &a, const vec3 &b);
vec3 mul(const vec3 &a, double s);
double dot(const vec3 &a, const vec3 &b);

// The near plane both the view and the filled-mesh pass clip against.
inline constexpr double rv_editor_near = 0.05;
// Corners of a triangle.
inline constexpr int tri_vertices = 3;

// The editor camera as it is this frame: its frame and how a point lands on screen.
struct rv_editor_view {
    vec3 eye, right, up, forward;
    ImVec2 center;
    double focal = 1.0;

    vec3 to_view(const vec3 &p) const;
    // Precondition: v[2] >= rv_editor_near up to rounding; rv_editor_draw_filled (a triangle wholly
    // in front) and rv_editor_clip_near (its pieces) guarantee it.
    ImVec2 to_screen(const vec3 &v) const;
    // False when the point is behind the near plane.
    bool point(const vec3 &p, ImVec2 &out) const;
};

rv_editor_view rv_editor_view_make(const rv_editor_scene_camera &c, ImVec2 p0, ImVec2 size);
// A scene segment clipped to the near plane, drawn.
void rv_editor_line(ImDrawList *dl, const rv_editor_view &v, const vec3 &a, const vec3 &b, ImU32 color, float width);
float rv_editor_segment_distance(ImVec2 p, ImVec2 a, ImVec2 b);
// Each object's edges in scene space: a mesh's real triangles, .glb or .obj (or the unit cube while
// it does not resolve, with the reason in *error), a camera's body and lens, a group's corner
// brackets and axis cross, a third marker for any other kind.
std::vector<std::pair<vec3, vec3>>
rv_editor_object_edges(const rv_editor_scene &scene, const rv_editor_project &project, int index, std::string *error = nullptr);

struct rv_editor_cpu_image;

struct rv_editor_tri {
    vec3 p[3];
    ImVec2 uv[3];
    // A .glb's triangle: from_model, so the object's tint applies to it as in the console, and each
    // corner's colour 0..1 (vertex colour times part colour). A mesh without them keeps from_model false and white.
    bool from_model = false;
    // The same part's picture decoded to memory, for the CPU rasterizer; null for none. It points into
    // the .glb cache (glb.cpp): a set is freed on the first successful re-read after the file's mtime
    // moved, or when the file is gone; a failed re-read keeps it. Read cpu_tex before the next
    // rv_editor_object_triangles call.
    const rv_editor_cpu_image *cpu_tex = nullptr;
    ImVec4 color[3] = { { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } };
};

// The triangle cut to the near plane (view z >= rv_editor_near): 0 to 2 triangles in the same winding,
// each cut corner's uv and colour interpolated with its position. Wholly in front: the triangle as is;
// wholly behind: none.
std::vector<rv_editor_tri> rv_editor_clip_near(const rv_editor_view &v, const rv_editor_tri &tri);

// An object's surface in scene space: a mesh's triangles, a .glb's (name ends in ".glb") or an
// .obj's, or the unit cube while its mesh is empty or does not load (the reason in *error when
// given); a quad's two triangles (rv_pdklib::rv_scene_quad_corners' layout, uv the object's
// pixel rect); empty for other kinds (a billboard's corners depend on the view: see
// rv_editor_billboard_triangles).
// Each mesh triangle carries its uv: a .glb's as glTF has it, an .obj's vt (or 0,0 for a corner
// without one; the whole unit square per face for the placeholder cube); a quad's uv is raw pixels, normalized in
// rv_editor_draw_filled once a texture's pixel size is known.
// Each .glb part's cpu_tex is set.
std::vector<rv_editor_tri> rv_editor_object_triangles(const rv_editor_scene &scene,
    const rv_editor_project &project,
    int index,
    std::string *error = nullptr);
// A billboard's two triangles: a card of world size scale.xy (a scaled parent included) centred on
// the object's world position, facing the view's right/up (not the object's own rotation).
std::vector<rv_editor_tri>
rv_editor_billboard_triangles(const rv_editor_view &v, const rv_editor_affine &m, const rv_editor_scene_object &o);
// A flat disc texture name resolved to its project file under [textures]; empty when none matches.
std::filesystem::path rv_editor_resolve_texture_name(const rv_editor_project &project, const std::string &name);
// Every mesh, quad and billboard filled into the viewport's picture (rv_editor_scene_raster.cpp):
// a CPU z-buffer of `size` at `origin` in framebuffer pixels, drawn as one image on `dl`, transparent
// where nothing is. Flat-shaded by face normal; the selected one tinted; volumes never filled.
// `textured` false: flat colours only. True: a .glb part's picture, else the object's texture where
// it resolves and loads. A .glb's flat parts are their colour and take the object's tint; the
// selected object keeps its selection colour. `renderer` owns the picture's SDL texture, one per
// viewport (keyed by the ImGui ID of the viewport's tile). `origin` and `size` are the viewport's frame: rv_editor_view
// keeps only its centre and focal, so the frame is passed in.
void rv_editor_draw_filled(ImDrawList *dl,
    const rv_editor_view &v,
    const rv_editor_scene &scene,
    const rv_editor_project &project,
    const std::string &selected,
    ImU32 base,
    ImU32 selected_color,
    SDL_Renderer *renderer,
    ImVec2 origin,
    ImVec2 size,
    bool textured);
// Destroys every viewport's picture texture. Call after the last frame and before SDL_DestroyRenderer:
// a texture is bound to the renderer that made it, and a new renderer may reuse its address.
void rv_editor_scene_pictures_free();
// The object whose drawing passes nearest the click, within a few pixels; empty for none.
std::string rv_editor_pick(const rv_editor_scene &scene, const rv_editor_project &project, const rv_editor_view &v, ImVec2 at);
// The Move gizmo's axis under the mouse, or -1.
int rv_editor_gizmo_axis(const rv_editor_scene &scene,
    const std::string &selected,
    const rv_editor_view &v,
    ImVec2 mouse,
    double length);

} // namespace rv_editor
