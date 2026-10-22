#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "pdklib/rv_gte/rv_gte_camera.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "project/rv_editor_project.hpp"
#include "scene/rv_editor_scene_edit.hpp"

struct SDL_Renderer;

namespace rv_editor
{

struct rv_editor_app;
struct rv_editor_glb_model;
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

// The near plane in metres. The Q20.12 plane both passes clip against is rv_editor_near_fx.
inline constexpr double rv_editor_near = 0.05;
// The editor's Q20.12 raw of rv_editor_near, rounded to the nearest step.
inline constexpr rv_pdklib::fx rv_editor_near_fx = static_cast<rv_pdklib::fx>(rv_editor_near * rv_pdklib::fx_one + 0.5);
// Corners of a triangle.
inline constexpr int tri_vertices = 3;
// Seed of a box's min/max: lo starts at +it and hi at -it, and the first coordinate replaces it.
inline constexpr double bounding_box_infinity = 1e30;

// The editor camera as it is this frame: the integer camera (rv_gte_camera) and the screen centre.
// right, up, forward are its rows as doubles, exact 4.12 values, for the viewport tools.
struct rv_editor_view {
    rv_pdklib::rv_gte_camera camera{};
    vec3 right{}, up{}, forward{};
    ImVec2 center{};
    // False when the camera did not build: the camera is then zero, and the viewport draws no scene.
    bool valid = false;

    vec3 to_view(const vec3 &p) const;
    // Precondition: v[2] >= rv_editor_near up to rounding; the filled pass passes only the corners of
    // the pieces rv_editor_clip_near_fx returns, which are at or in front of the plane.
    ImVec2 to_screen(const vec3 &v) const;
    // False when the point is behind the near plane.
    bool point(const vec3 &p, ImVec2 &out) const;
};

rv_editor_view rv_editor_view_make(const rv_editor_scene_camera &c, ImVec2 p0, ImVec2 size);
// A scene segment clipped to the near plane, drawn.
void rv_editor_line(ImDrawList *dl, const rv_editor_view &v, const vec3 &a, const vec3 &b, ImU32 color, float width);
float rv_editor_segment_distance(ImVec2 p, ImVec2 a, ImVec2 b);
// Each object's edges in scene space: a mesh's real .glb triangles (or the unit cube while
// its mesh is empty or does not load, with the reason in *error; a legacy quad or billboard is that cube),
// a camera's body and lens, a group's corner brackets and axis cross, a third marker for any other kind.
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

// A corner for the integer clip: its view-space point as Q20.12, with the uv and colour the editor carries.
struct rv_editor_fx_vertex {
    rv_pdklib::fx_vec3 p{};
    ImVec2 uv{};
    ImVec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
};

struct rv_editor_fx_tri {
    rv_editor_fx_vertex v[tri_vertices];
};

// Most triangles one triangle gives after the near cut: a convex polygon of at most four corners, fanned.
inline constexpr int rv_editor_fx_max_pieces = 2;

// Cuts a triangle to the near plane in integers (rv_editor_scene_clip.cpp), on the console's rv_gte primitives.
// near_plane is Q20.12 raw. Sets count to 0, 1 or 2 and writes those triangles to out, in the source
// winding: wholly in front, the triangle as is; wholly behind, none. Error: a cut point leaves fx; count is 0, out unused.
int rv_editor_clip_near_fx(const rv_editor_fx_tri &tri,
    rv_pdklib::fx near_plane,
    rv_editor_fx_tri (&out)[rv_editor_fx_max_pieces],
    int &count);

// An object's surface in scene space: a mesh's .glb triangles (name ends in ".glb"), or the unit
// cube while its mesh is empty or does not load (the reason in *error when given). A legacy quad
// or billboard has no model and is the unit cube too. Empty for other kinds.
// Each mesh triangle carries its glTF uv (the whole unit square per face for the placeholder
// cube). Each .glb part's cpu_tex is set.
std::vector<rv_editor_tri> rv_editor_object_triangles(const rv_editor_scene &scene,
    const rv_editor_project &project,
    int index,
    std::string *error = nullptr);
// A flat disc texture name resolved to its project file under [textures]; empty when none matches.
std::filesystem::path rv_editor_resolve_texture_name(const rv_editor_project &project, const std::string &name);
// Every mesh, quad and billboard shown (not outlined or absent) filled into the viewport's picture
// (rv_editor_scene_raster.cpp): a CPU z-buffer of `size` at `origin` in framebuffer pixels, drawn as one
// image on `dl`, transparent where nothing is. Flat-shaded by face normal; the selected one tinted; volumes never filled.
// `textured` false: flat colours only. True: a .glb part's picture, else the object's texture where
// it resolves and loads. A .glb's flat parts are their colour and take the object's tint; the
// selected object keeps its selection colour. `renderer` owns the picture's SDL texture, one per
// viewport (keyed by the ImGui ID of the viewport's tile). `origin` and `size` are the viewport's frame: rv_editor_view
// keeps only its centre and camera, so the frame is passed in.
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
    bool textured);
// Destroys every viewport's picture texture. Call after the last frame and before SDL_DestroyRenderer:
// a texture is bound to the renderer that made it, and a new renderer may reuse its address.
void rv_editor_scene_pictures_free();
// The object whose drawing passes nearest the click, within a few pixels; empty for none. Absent objects are not picked.
std::string
rv_editor_pick(const rv_editor_scene_doc &doc, const rv_editor_project &project, const rv_editor_view &v, ImVec2 at);
// Parts mode (rv_editor_scene_parts.cpp): the nearest model triangle under `at` among the visible meshes,
// its object's id and piece number from 1. RV_OK, or RV_ERR_NOENT for none.
int rv_editor_parts_pick(const rv_editor_scene_doc &doc,
    const rv_editor_project &project,
    const rv_editor_view &v,
    ImVec2 at,
    std::string &owner,
    size_t &piece,
    std::shared_ptr<const rv_editor_glb_model> &model);
// A click in Parts mode: picks the piece alone, Shift toggles it among the selected object's picks.
// A miss clears the selection, unless Shift is held.
void rv_editor_parts_click(rv_editor_scene_doc &doc,
    const rv_editor_project &project,
    const rv_editor_view &v,
    ImVec2 at,
    bool shift);
// The picked pieces to draw this frame: the selected object's index in doc.scene.objects and its model.
// A null model means no pieces are drawn: nothing picked, the picks on another object, an absent
// object, a model other than the picks', a model that does not load, or a piece count that differs from the picks'.
struct rv_editor_parts_target {
    int owner = -1;
    std::shared_ptr<const rv_editor_glb_model> model;
};
// The viewport resolves it once per frame; Frame resolves it when pressed.
// The owner is filled as unselected only when model is set.
rv_editor_parts_target rv_editor_parts_resolve(const rv_editor_scene_doc &doc, const rv_editor_project &project);
// The world-space box of the target's picked pieces; false when none are drawn or they hold no triangles.
bool rv_editor_parts_bounds(const rv_editor_scene_doc &doc, const rv_editor_parts_target &target, vec3 &lo, vec3 &hi);
// The picked pieces of the selected object drawn as edges in `color`, from rv_editor_parts_resolve.
void rv_editor_parts_highlight(ImDrawList *dl,
    const rv_editor_view &v,
    const rv_editor_scene_doc &doc,
    const rv_editor_parts_target &target,
    ImU32 color);
// The Move gizmo's axis under the mouse, or -1.
int rv_editor_gizmo_axis(const rv_editor_scene &scene,
    const std::string &selected,
    const rv_editor_view &v,
    ImVec2 mouse,
    double length);

} // namespace rv_editor
