// Scene viewport configuration: toolbar presets, tool selection, and object manipulation
#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "imgui.h"

#include "panes/rv_editor_scene_draw.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

double rv_editor_snap(double v, double step)
{
    return std::round(v / step) * step;
}

constexpr int space_axes = 3;
constexpr double half = 0.5;
constexpr double rotation_speed_factor = 0.5;
constexpr double rotation_snap_degrees = 15.0;
constexpr double scale_sensitivity = 0.01;
constexpr double scale_min = 0.1;
constexpr double scale_snap_step = 0.1;
constexpr double bounding_box_infinity = 1e30;
constexpr double frame_distance_base = 2.0;
constexpr double frame_distance_multiplier = 1.6;
constexpr double perspective_view_yaw = 30.0;
constexpr double perspective_view_pitch = 25.0;
constexpr double top_view_pitch = 89.9;
constexpr double right_view_yaw = -90.0;
constexpr float dropdown_width_factor = 5.0f;
constexpr double snap_step_epsilon = 1e-9;

} // namespace

// A drag of the selected object by the Toolchest's tool: along one axis for Move,
// by the mouse for Rotate (yaw across, pitch up and down) and Scale (uniform).
void rv_editor_manipulate(rv_editor_app &app, const rv_editor_view &v)
{
    rv_editor_scene_doc &doc = *app.scene;
    rv_editor_scene_ui &ui = app.scene_ui;
    const int at = rv_editor_scene_find(doc.scene, doc.selected);
    if (at < 0) {
        return;
    }
    rv_editor_scene_object &o = doc.scene.objects[static_cast<size_t>(at)];
    const ImVec2 total(ImGui::GetMousePos().x - ui.gizmo_from[0], ImGui::GetMousePos().y - ui.gizmo_from[1]);
    const rv_editor_scene_camera &cam = ui.camera;
    if (cam.tool == rv_editor_scene_tool::move) {
        // Pixels along the axis as the screen shows it, into scene units, into the parent's space.
        const vec3 origin = rv_editor_affine_point(rv_editor_scene_world(doc.scene, at), { 0, 0, 0 });
        vec3 axis{ 0, 0, 0 };
        axis[static_cast<size_t>(ui.gizmo_axis)] = 1;
        ImVec2 s0, s1;
        if (!v.point(origin, s0) || !v.point(add(origin, axis), s1)) {
            return;
        }
        const double dx = s1.x - s0.x, dy = s1.y - s0.y;
        const double len2 = dx * dx + dy * dy;
        if (len2 < 1.0) {
            return;
        }
        const double units = (total.x * dx + total.y * dy) / len2;
        vec3 world = mul(axis, units);
        const int parent = rv_editor_scene_find(doc.scene, o.parent);
        if (parent >= 0) {
            const rv_editor_affine pw = rv_editor_scene_world(doc.scene, parent);
            world = rv_editor_affine_solve(pw, world);
        }
        o.position = add(ui.gizmo_before_position, world);
        if (cam.snap) {
            for (double &c : o.position) {
                c = rv_editor_snap(c, cam.snap_step);
            }
        }
    } else if (cam.tool == rv_editor_scene_tool::rotate) {
        o.rotation = ui.gizmo_before_rotation;
        o.rotation[1] += total.x * rotation_speed_factor;
        o.rotation[0] += total.y * rotation_speed_factor;
        if (cam.snap) {
            o.rotation[0] = rv_editor_snap(o.rotation[0], rotation_snap_degrees);
            o.rotation[1] = rv_editor_snap(o.rotation[1], rotation_snap_degrees);
        }
    } else if (cam.tool == rv_editor_scene_tool::scale) {
        const double f = std::exp(-total.y * scale_sensitivity);
        o.scale = mul(ui.gizmo_before_scale, f);
        if (cam.snap) {
            for (double &c : o.scale) {
                c = std::max(scale_min, rv_editor_snap(c, scale_snap_step));
            }
        }
    }
}

void rv_editor_scene_frame(rv_editor_app &app, bool all)
{
    rv_editor_scene_doc &doc = *app.scene;
    rv_editor_scene_camera &cam = app.scene_ui.camera;
    vec3 lo{ bounding_box_infinity, bounding_box_infinity, bounding_box_infinity },
        hi{ -bounding_box_infinity, -bounding_box_infinity, -bounding_box_infinity };
    bool any = false;
    for (size_t i = 0; i < doc.scene.objects.size(); ++i) {
        if (!all && doc.scene.objects[i].id != doc.selected) {
            continue;
        }
        for (const auto &[a, b] : rv_editor_object_edges(doc.scene, app.project, static_cast<int>(i))) {
            for (const vec3 &p : { a, b }) {
                for (size_t k = 0; k < space_axes; ++k) {
                    lo[k] = std::min(lo[k], p[k]);
                    hi[k] = std::max(hi[k], p[k]);
                }
                any = true;
            }
        }
    }
    if (!any) {
        return;
    }
    cam.target = mul(add(lo, hi), half);
    cam.distance = std::max(frame_distance_base, std::sqrt(dot(sub(hi, lo), sub(hi, lo))) * frame_distance_multiplier);
}

void rv_editor_scene_toolbar(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_scene_camera &cam = app.scene_ui.camera;
    const struct
    {
        const char *label_id;
        double yaw, pitch;
    } views[] = {
        { "scene_tools.view_persp", perspective_view_yaw, perspective_view_pitch },
        { "scene_tools.view_top", 0.0, top_view_pitch },
        { "scene_tools.view_front", 0.0, 0.0 },
        { "scene_tools.view_right", right_view_yaw, 0.0 }
    };
    for (const auto &view : views) {
        const char *label = rv_editor_text(view.label_id);
        rv_editor_flow(rv_editor_button_width(label));
        if (rv_editor_button(label, theme)) {
            cam.yaw = view.yaw;
            cam.pitch = view.pitch;
        }
    }
    const char *no_selection = app.scene->selected.empty() ? rv_editor_text("scene_tools.nothing_selected") : nullptr;
    const char *frame_btn = rv_editor_text("scene_tools.toolbar_frame_selection");
    rv_editor_flow(rv_editor_button_width(frame_btn));
    if (rv_editor_button(frame_btn, theme, { rv_editor_look::live, no_selection })) {
        rv_editor_scene_frame(app, false);
    }
    ImGui::SetItemTooltip("%s", rv_editor_text("scene_tools.toolbar_frame_tooltip"));
    const char *view_all = rv_editor_text("scene_tools.toolbar_view_all");
    rv_editor_flow(rv_editor_button_width(view_all));
    if (rv_editor_button(view_all, theme)) {
        rv_editor_scene_frame(app, true);
    }
    const char *home = rv_editor_text("scene_tools.toolbar_home");
    rv_editor_flow(rv_editor_button_width(home));
    if (rv_editor_button(home, theme)) {
        cam = rv_editor_scene_camera{ cam.tool, cam.snap, cam.snap_step, cam.grid, cam.shading };
    }
    ImGui::SetItemTooltip("%s", rv_editor_text("scene_tools.toolbar_home_tooltip"));
    const char *seek = rv_editor_text("scene_tools.toolbar_seek");
    rv_editor_flow(rv_editor_button_width(seek));
    bool seek_val = cam.seeking;
    if (rv_editor_toggle(seek, &seek_val, theme)) {
        cam.seeking = seek_val;
    }
    ImGui::SetItemTooltip("%s", rv_editor_text("scene_tools.toolbar_seek_tooltip"));
    const struct
    {
        const char *label_id;
        const char *tip_id;
        rv_editor_scene_shading shading;
    } shadings[] = {
        { "scene_tools.shading_wireframe", "scene_tools.shading_wireframe_tooltip", rv_editor_scene_shading::wireframe },
        { "scene_tools.shading_filled", "scene_tools.shading_filled_tooltip", rv_editor_scene_shading::filled },
        { "scene_tools.shading_textured", "scene_tools.shading_textured_tooltip", rv_editor_scene_shading::textured },
    };
    for (const auto &s : shadings) {
        const char *label = rv_editor_text(s.label_id);
        rv_editor_flow(rv_editor_button_width(label));
        if (rv_editor_radio(label, cam.shading == s.shading, theme)) {
            cam.shading = s.shading;
        }
        ImGui::SetItemTooltip("%s", rv_editor_text(s.tip_id));
    }
}

void rv_editor_scene_tools(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_scene_camera &cam = app.scene_ui.camera;
    const char *const tool_ids[] = {
        "scene_tools.tool_select", "scene_tools.tool_move", "scene_tools.tool_rotate", "scene_tools.tool_scale"
    };
    for (int t = 0; t < std::ssize(tool_ids); ++t) {
        const char *tool_name = rv_editor_text(tool_ids[t]);
        if (rv_editor_radio(tool_name, cam.tool == static_cast<rv_editor_scene_tool>(t), theme)) {
            cam.tool = static_cast<rv_editor_scene_tool>(t);
        }
    }
    const char *snap_label = rv_editor_text("scene_tools.snap_label");
    rv_editor_checkbox(snap_label, &cam.snap, theme);
    ImGui::SetItemTooltip("%s", rv_editor_text("scene_tools.snap_tooltip"));
    const char *const steps[] = { "0.1", "0.25", "0.5", "1" };
    const double values[] = { 0.1, 0.25, 0.5, 1.0 };
    int step = 1;
    for (int i = 0; i < std::ssize(values); ++i) {
        step = std::fabs(cam.snap_step - values[i]) < snap_step_epsilon ? i : step;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * dropdown_width_factor);
    if (rv_editor_dropdown("##snapstep", &step, steps, std::ssize(steps), theme)) {
        cam.snap_step = values[step];
    }
    ImGui::SetItemTooltip("%s", rv_editor_text("scene_tools.snap_step_tooltip"));
    const char *grid_label = rv_editor_text("scene_tools.grid_label");
    rv_editor_checkbox(grid_label, &cam.grid, theme);
}

} // namespace rv_editor
