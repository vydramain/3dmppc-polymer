// The Scene viewport's picking: the object under the mouse and the Move gizmo's axis under it.

#include "panes/rv_editor_scene_draw.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_gte/rv_gte_matrix.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "scene/rv_editor_scene.hpp"

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

int rv_editor_gizmo_origin(const rv_editor_scene &scene, int at, rv_pdklib::fx_vec3 &o)
{
    static bool reported = false;
    rv_pdklib::rv_gte_matrix world{};
    const int rc = rv_editor_scene_world_gte(scene, at, world);
    if (rc != RV_OK && !reported) {
        reported = true;
        RV_LOG_WARN(scene_log_tag, "the gizmo's origin cannot be built in Q20.12 (rc {}): no gizmo for this object", rc);
    }
    if (rc != RV_OK) {
        return rc;
    }
    o = { world.t[0], world.t[1], world.t[2] };
    return RV_OK;
}

int rv_editor_gizmo_tip(const rv_pdklib::fx_vec3 &o, int axis, rv_pdklib::fx length, rv_pdklib::fx_vec3 &tip)
{
    static bool reported = false;
    const rv_pdklib::fx_vec3 leg{ axis == 0 ? length : 0, axis == 1 ? length : 0, axis == 2 ? length : 0 };
    const int rc = rv_pdklib::fx_vec3_add(o, leg, tip);
    if (rc != RV_OK && !reported) {
        reported = true;
        RV_LOG_WARN(scene_log_tag, "a gizmo arm's tip is outside Q20.12 (rc {}): that arm is left out", rc);
    }
    return rc;
}

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

// The Move gizmo's axis under the mouse, or -1. The arms are Q20.12 and `length` is their raw length.
int rv_editor_gizmo_axis(const rv_editor_scene &scene,
    const std::string &selected,
    const rv_editor_view &v,
    ImVec2 mouse,
    rv_pdklib::fx length)
{
    const int at = rv_editor_scene_find(scene, selected);
    if (at < 0) {
        return -1;
    }
    rv_pdklib::fx_vec3 o{};
    if (rv_editor_gizmo_origin(scene, at, o) != RV_OK) {
        return -1;
    }
    for (int a = 0; a < axis_count; ++a) {
        rv_pdklib::fx_vec3 tip{};
        if (rv_editor_gizmo_tip(o, a, length, tip) != RV_OK) {
            continue;
        }
        // The same edge the viewport draws: an arm wholly behind the near plane, or outside Q20.12, is not hit.
        ImVec2 so, st;
        if (rv_editor_edge_screen_fx(v, o, tip, so, st) == RV_OK &&
            rv_editor_segment_distance(mouse, so, st) < pointer_proximity_threshold_px) {
            return a;
        }
    }
    return -1;
}

} // namespace rv_editor
