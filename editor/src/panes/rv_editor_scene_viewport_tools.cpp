// Scene viewport configuration: toolbar presets, tool selection, and object manipulation
#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "imgui.h"

#include "panes/rv_editor_scene_draw.hpp"
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
        axis[static_cast<size_t>(ui.gizmo_axis)] = 1.0;
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
        o.rotation[1] += total.x * 0.5;
        o.rotation[0] += total.y * 0.5;
        if (cam.snap) {
            o.rotation[0] = rv_editor_snap(o.rotation[0], 15.0);
            o.rotation[1] = rv_editor_snap(o.rotation[1], 15.0);
        }
    } else if (cam.tool == rv_editor_scene_tool::scale) {
        const double f = std::exp(-total.y * 0.01);
        o.scale = mul(ui.gizmo_before_scale, f);
        if (cam.snap) {
            for (double &c : o.scale) {
                c = std::max(0.1, rv_editor_snap(c, 0.1));
            }
        }
    }
}

void rv_editor_scene_frame(rv_editor_app &app, bool all)
{
    rv_editor_scene_doc &doc = *app.scene;
    rv_editor_scene_camera &cam = app.scene_ui.camera;
    vec3 lo{ 1e30, 1e30, 1e30 }, hi{ -1e30, -1e30, -1e30 };
    bool any = false;
    for (size_t i = 0; i < doc.scene.objects.size(); ++i) {
        if (!all && doc.scene.objects[i].id != doc.selected) {
            continue;
        }
        for (const auto &[a, b] : rv_editor_object_edges(doc.scene, app.project, static_cast<int>(i))) {
            for (const vec3 &p : { a, b }) {
                for (size_t k = 0; k < 3; ++k) {
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
    cam.target = mul(add(lo, hi), 0.5);
    cam.distance = std::max(2.0, std::sqrt(dot(sub(hi, lo), sub(hi, lo))) * 1.6);
}

void rv_editor_scene_toolbar(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_scene_camera &cam = app.scene_ui.camera;
    const struct
    {
        const char *label;
        double yaw, pitch;
    } views[] = { { "Persp", 30.0, 25.0 }, { "Top", 0.0, 89.9 }, { "Front", 0.0, 0.0 }, { "Right", -90.0, 0.0 } };
    for (const auto &view : views) {
        rv_editor_flow(rv_editor_button_width(view.label));
        if (rv_editor_button(view.label, theme)) {
            cam.yaw = view.yaw;
            cam.pitch = view.pitch;
        }
    }
    const char *no_selection = app.scene->selected.empty() ? "Nothing is selected" : nullptr;
    rv_editor_flow(rv_editor_button_width("Frame Selection"));
    if (rv_editor_button("Frame Selection", theme, { rv_editor_look::live, no_selection })) {
        rv_editor_scene_frame(app, false);
    }
    ImGui::SetItemTooltip("F: the selected object fills the view");
    rv_editor_flow(rv_editor_button_width("View All"));
    if (rv_editor_button("View All", theme)) {
        rv_editor_scene_frame(app, true);
    }
    rv_editor_flow(rv_editor_button_width("Home"));
    if (rv_editor_button("Home", theme)) {
        cam = rv_editor_scene_camera{ cam.tool, cam.snap, cam.snap_step, cam.grid, cam.shading };
    }
    ImGui::SetItemTooltip("Home key: the view the scene opened with");
    rv_editor_flow(rv_editor_button_width("Seek"));
    bool seek = cam.seeking;
    if (rv_editor_toggle("Seek", &seek, theme)) {
        cam.seeking = seek;
    }
    ImGui::SetItemTooltip("Then click an object: the view turns about it");
    const struct
    {
        const char *label;
        rv_editor_scene_shading shading;
        const char *tip;
    } shadings[] = {
        { "Wireframe", rv_editor_scene_shading::wireframe, "Wireframe: edges only" },
        { "Filled", rv_editor_scene_shading::filled, "Filled: shaded polygons" },
        { "Textured", rv_editor_scene_shading::textured, "Textured: meshes with their scene texture; untextured ones flat" },
    };
    for (const auto &s : shadings) {
        rv_editor_flow(rv_editor_button_width(s.label));
        if (rv_editor_radio(s.label, cam.shading == s.shading, theme)) {
            cam.shading = s.shading;
        }
        ImGui::SetItemTooltip("%s", s.tip);
    }
}

void rv_editor_scene_tools(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_scene_camera &cam = app.scene_ui.camera;
    const char *const names[] = { "Select (Q)", "Move (W)", "Rotate (E)", "Scale (R)" };
    for (int t = 0; t < 4; ++t) {
        if (rv_editor_radio(names[t], cam.tool == static_cast<rv_editor_scene_tool>(t), theme)) {
            cam.tool = static_cast<rv_editor_scene_tool>(t);
        }
    }
    rv_editor_checkbox("Snap", &cam.snap, theme);
    ImGui::SetItemTooltip("Move by the step below, turn by 15 degrees, scale by 0.1");
    const char *const steps[] = { "0.1", "0.25", "0.5", "1" };
    const double values[] = { 0.1, 0.25, 0.5, 1.0 };
    int step = 1;
    for (int i = 0; i < 4; ++i) {
        step = std::fabs(cam.snap_step - values[i]) < 1e-9 ? i : step;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
    if (rv_editor_dropdown("##snapstep", &step, steps, 4, theme)) {
        cam.snap_step = values[step];
    }
    ImGui::SetItemTooltip("The Move snap step, in scene units");
    rv_editor_checkbox("Grid", &cam.grid, theme);
}

} // namespace rv_editor
