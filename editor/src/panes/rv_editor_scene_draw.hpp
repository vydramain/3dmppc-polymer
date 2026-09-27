#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "scene/rv_editor_scene_edit.hpp"

namespace rv_editor
{

// The Scene viewport's geometry (editor/src/panes/rv_editor_scene_viewport.cpp):
// the editor camera's frame, how scene points land on screen, what each object
// draws as, and what the mouse is over.

using vec3 = std::array<double, 3>;

vec3 sub(const vec3 &a, const vec3 &b);
vec3 add(const vec3 &a, const vec3 &b);
vec3 mul(const vec3 &a, double s);
double dot(const vec3 &a, const vec3 &b);

struct rv_editor_scene_camera;

// The editor camera as it is this frame: its frame and how a point lands on screen.
struct rv_editor_view
{
    vec3 eye, right, up, forward;
    ImVec2 center;
    double focal = 1.0;

    vec3 to_view(const vec3 &p) const;
    ImVec2 to_screen(const vec3 &v) const;
    // False when the point is behind the near plane.
    bool point(const vec3 &p, ImVec2 &out) const;
};

rv_editor_view rv_editor_view_make(const rv_editor_scene_camera &c, ImVec2 p0, ImVec2 size);
// A scene segment clipped to the near plane, drawn.
void rv_editor_line(ImDrawList *dl, const rv_editor_view &v, const vec3 &a, const vec3 &b, ImU32 color, float width);
float rv_editor_segment_distance(ImVec2 p, ImVec2 a, ImVec2 b);
// Each object's edges in scene space: a box's twelve, a camera's pyramid, a group's axes.
std::vector<std::pair<vec3, vec3>> rv_editor_object_edges(const rv_editor_scene &scene, int index);
// The object whose drawing passes nearest the click, within a few pixels; empty for none.
std::string rv_editor_pick(const rv_editor_scene &scene, const rv_editor_view &v, ImVec2 at);
// The Move gizmo's axis under the mouse, or -1.
int rv_editor_gizmo_axis(const rv_editor_scene &scene, const std::string &selected, const rv_editor_view &v,
    ImVec2 mouse, double length);

} // namespace rv_editor
