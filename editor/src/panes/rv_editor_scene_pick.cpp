// The Scene viewport's picking: the object under the mouse and the Move gizmo's axis under it.

#include "panes/rv_editor_scene_draw.hpp"
#include "pdk/rv_err.h"

#include <algorithm>
#include <cmath>

namespace rv_editor
{

namespace
{

// Number of spatial axes.
constexpr int axis_count = 3;

// Pointer proximity threshold for interactive picking and gizmo interaction.
constexpr float pointer_proximity_threshold_px = 6.0f;

} // namespace

float rv_editor_segment_distance(ImVec2 p, ImVec2 a, ImVec2 b)
{
    const ImVec2 ab(b.x - a.x, b.y - a.y);
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    const float t = len2 > 0.0f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
    const float dx = a.x + ab.x * t - p.x, dy = a.y + ab.y * t - p.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::string rv_editor_pick(const rv_editor_scene_doc &doc, const rv_editor_project &project, const rv_editor_view &v, ImVec2 at)
{
    const rv_editor_scene &scene = doc.scene;
    std::string best;
    float best_d = pointer_proximity_threshold_px;
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        if (rv_editor_scene_visibility(doc, scene.objects[i]) == rv_editor_visibility::absent) {
            continue;
        }
        // Runs on each click, so it stays silent about edges the fx builder leaves out: the viewport names those objects.
        int left_out = 0;
        for (const auto &[a, b] : rv_editor_object_edges_fx(scene, project, static_cast<int>(i), left_out)) {
            // Same edges the viewport draws: NOENT (wholly behind) and INVAL (outside Q20.12) are left undrawn.
            ImVec2 sa, sb;
            if (rv_editor_edge_screen_fx(v, a, b, sa, sb) != RV_OK) {
                continue;
            }
            const float d = rv_editor_segment_distance(at, sa, sb);
            if (d < best_d) {
                best_d = d;
                best = scene.objects[i].id;
            }
        }
    }
    return best;
}

// The Move gizmo's axis under the mouse, or -1.
int rv_editor_gizmo_axis(const rv_editor_scene &scene,
    const std::string &selected,
    const rv_editor_view &v,
    ImVec2 mouse,
    double length)
{
    const int at = rv_editor_scene_find(scene, selected);
    if (at < 0) {
        return -1;
    }
    const vec3 o = rv_editor_affine_point(rv_editor_scene_world(scene, at), { 0, 0, 0 });
    ImVec2 so;
    if (!v.point(o, so)) {
        return -1;
    }
    for (int a = 0; a < axis_count; ++a) {
        vec3 tip = o;
        tip[static_cast<size_t>(a)] += length;
        ImVec2 st;
        if (v.point(tip, st) && rv_editor_segment_distance(mouse, so, st) < pointer_proximity_threshold_px) {
            return a;
        }
    }
    return -1;
}

} // namespace rv_editor
