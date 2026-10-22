// Scene viewport configuration: toolbar presets, tool selection, and object manipulation
#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_parts_ops.hpp"
#include "panes/rv_editor_scene_draw.hpp"
#include "pdk/rv_err.h"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_glyphs.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_text.hpp"

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
constexpr double frame_distance_base = 2.0;
constexpr double frame_distance_multiplier = 1.6;
constexpr double perspective_view_yaw = 30.0;
constexpr double perspective_view_pitch = 25.0;
constexpr double top_view_pitch = 89.9;
constexpr double right_view_yaw = -90.0;
constexpr float dropdown_width_factor = 5.0f;
constexpr double snap_step_epsilon = 1e-9;
// Preset angles are exact; a drag leaves the camera off them by more than this (degrees).
constexpr double view_match_epsilon = 1e-6;
constexpr double view_full_turn_degrees = 360.0;
// Blank space between toolbar groups, in font sizes.
constexpr float group_gap_factor = 0.5f;

// Snap step labels and values for the viewport tool dropdown.
constexpr const char *snap_step_labels[] = { "0.1", "0.25", "0.5", "1" };
constexpr double snap_step_values[] = { 0.1, 0.25, 0.5, 1.0 };
static_assert(std::size(snap_step_labels) == std::size(snap_step_values),
    "snap_step_labels and snap_step_values must have equal size");
// Default snap step choice index (0.25 matches camera_snap_step_default in rv_editor_app.hpp).
constexpr int default_snap_step_index = 1;

// One checkbox per layer of the scene; a layer switched off is not drawn or selected.
void rv_editor_scene_layers_menu(rv_editor_scene_doc &doc, const rv_editor_theme &theme)
{
    const std::vector<std::string> layers = rv_editor_scene_layers(doc.scene);
    if (layers.empty()) {
        rv_editor_label(rv_editor_text("scene_tools.layers_none"), rv_editor_label_kind::normal, theme);
        return;
    }
    for (const std::string &layer : layers) {
        bool on = !doc.off_layers.contains(layer);
        if (rv_editor_checkbox(layer.c_str(), &on, theme)) {
            if (on) {
                doc.off_layers.erase(layer);
            } else {
                doc.off_layers.insert(layer);
            }
        }
    }
}

// Scene view presets; the view dropdown shows the one the camera is at, if any.
struct rv_editor_scene_view {
    const char *label_id;
    double yaw, pitch;
};
constexpr rv_editor_scene_view scene_views[] = { { "scene_tools.view_persp", perspective_view_yaw, perspective_view_pitch },
    { "scene_tools.view_top", 0.0, top_view_pitch },
    { "scene_tools.view_front", 0.0, 0.0 },
    { "scene_tools.view_right", right_view_yaw, 0.0 } };

struct rv_editor_scene_shading_option {
    const char *label_id;
    const char *tip_id;
    rv_editor_scene_shading shading;
};
constexpr rv_editor_scene_shading_option scene_shadings[] = {
    { "scene_tools.shading_wireframe", "scene_tools.shading_wireframe_tooltip", rv_editor_scene_shading::wireframe },
    { "scene_tools.shading_filled", "scene_tools.shading_filled_tooltip", rv_editor_scene_shading::filled },
    { "scene_tools.shading_textured", "scene_tools.shading_textured_tooltip", rv_editor_scene_shading::textured },
};

// The preset the camera is at, or -1 when it is at none.
int rv_editor_scene_view_index(const rv_editor_scene_camera &cam)
{
    for (int i = 0; i < std::ssize(scene_views); ++i) {
        // The camera's yaw is not wrapped: a turn of 360 degrees is still the same view.
        const bool at_yaw =
            std::fabs(std::remainder(cam.yaw - scene_views[i].yaw, view_full_turn_degrees)) < view_match_epsilon;
        const bool at_pitch = std::fabs(cam.pitch - scene_views[i].pitch) < view_match_epsilon;
        if (at_yaw && at_pitch) {
            return i;
        }
    }
    return -1;
}

// Width a dropdown needs for its longest item: the text, the arrow button and the frame padding.
float rv_editor_scene_dropdown_width(const char *const items[], int count)
{
    float widest = 0.0f;
    for (int i = 0; i < count; ++i) {
        widest = std::max(widest, ImGui::CalcTextSize(items[i]).x);
    }
    return widest + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;
}

// "View:" and the view presets; a pick turns the camera to that preset's angles.
void rv_editor_scene_view_select(rv_editor_scene_camera &cam, const rv_editor_theme &theme)
{
    ImGui::AlignTextToFramePadding();
    rv_editor_label(rv_editor_text("scene_tools.view_label"), rv_editor_label_kind::normal, theme);
    const char *names[std::size(scene_views)];
    for (size_t i = 0; i < std::size(scene_views); ++i) {
        names[i] = rv_editor_text(scene_views[i].label_id);
    }
    int view = rv_editor_scene_view_index(cam);
    const float width = rv_editor_scene_dropdown_width(names, std::ssize(scene_views));
    rv_editor_flow(width);
    ImGui::SetNextItemWidth(width);
    if (rv_editor_dropdown("##scene_view", &view, names, std::ssize(scene_views), theme)) {
        cam.yaw = scene_views[view].yaw;
        cam.pitch = scene_views[view].pitch;
    }
}

// "Shade:" and the shading modes; the tooltip explains the one that is on.
void rv_editor_scene_shading_select(rv_editor_scene_camera &cam, const rv_editor_theme &theme)
{
    const char *label = rv_editor_text("scene_tools.shading_label");
    rv_editor_flow(ImGui::CalcTextSize(label).x);
    ImGui::AlignTextToFramePadding();
    rv_editor_label(label, rv_editor_label_kind::normal, theme);
    const char *names[std::size(scene_shadings)];
    int current = 0;
    for (size_t i = 0; i < std::size(scene_shadings); ++i) {
        names[i] = rv_editor_text(scene_shadings[i].label_id);
        if (cam.shading == scene_shadings[i].shading) {
            current = static_cast<int>(i);
        }
    }
    const float width = rv_editor_scene_dropdown_width(names, std::ssize(scene_shadings));
    rv_editor_flow(width);
    ImGui::SetNextItemWidth(width);
    if (rv_editor_dropdown("##scene_shading", &current, names, std::ssize(scene_shadings), theme)) {
        cam.shading = scene_shadings[current].shading;
    }
    rv_editor_item_tooltip(rv_editor_text(scene_shadings[current].tip_id));
}

// Blank space between toolbar groups, kept on the row like an item.
void rv_editor_scene_group_gap()
{
    const float gap = ImGui::GetFontSize() * group_gap_factor;
    rv_editor_flow(gap);
    ImGui::Dummy(ImVec2(gap, 0.0f));
}

// Aims the camera at the middle of the box lo..hi, backed off far enough to fit its diagonal.
void rv_editor_frame_box(rv_editor_scene_camera &cam, const vec3 &lo, const vec3 &hi)
{
    cam.target = mul(add(lo, hi), half);
    cam.distance = std::max(frame_distance_base, std::sqrt(dot(sub(hi, lo), sub(hi, lo))) * frame_distance_multiplier);
}

} // namespace

// A drag of the selected object by the Toolchest's tool: along one axis for Move,
// by the mouse for Rotate (yaw across, pitch up and down) and Scale (uniform).
void rv_editor_manipulate(rv_editor_app &app,
    const rv_editor_scene_camera &cam,
    const rv_editor_view &v,
    const rv_editor_gizmo_drag &drag)
{
    rv_editor_scene_doc &doc = *app.scene;
    const int at = rv_editor_scene_find(doc.scene, doc.selected);
    if (at < 0) {
        return;
    }
    rv_editor_scene_object &o = doc.scene.objects[static_cast<size_t>(at)];
    const ImVec2 total(ImGui::GetMousePos().x - drag.from[0], ImGui::GetMousePos().y - drag.from[1]);
    switch (cam.tool) {
    case rv_editor_scene_tool::move: {
        // Pixels along the axis as the screen shows it, into scene units, into the parent's space.
        const vec3 origin = rv_editor_affine_point(rv_editor_scene_world(doc.scene, at), { 0, 0, 0 });
        vec3 axis{ 0, 0, 0 };
        axis[static_cast<size_t>(drag.axis)] = 1;
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
        rv_editor_vec3 position = add(drag.before_position, world);
        if (cam.snap) {
            for (double &c : position) {
                c = rv_editor_snap(c, cam.snap_step);
            }
        }
        rv_editor_object_set_position(o, position);
        break;
    }
    case rv_editor_scene_tool::rotate: {
        rv_editor_vec3 rotation = drag.before_rotation;
        rotation[1] += total.x * rotation_speed_factor;
        rotation[0] += total.y * rotation_speed_factor;
        if (cam.snap) {
            rotation[0] = rv_editor_snap(rotation[0], rotation_snap_degrees);
            rotation[1] = rv_editor_snap(rotation[1], rotation_snap_degrees);
        }
        rv_editor_object_set_rotation(o, rotation);
        break;
    }
    case rv_editor_scene_tool::scale: {
        const double f = std::exp(-total.y * scale_sensitivity);
        rv_editor_vec3 scale = mul(drag.before_scale, f);
        if (cam.snap) {
            for (double &c : scale) {
                c = std::max(scale_min, rv_editor_snap(c, scale_snap_step));
            }
        }
        rv_editor_object_set_scale(o, scale);
        break;
    }
    case rv_editor_scene_tool::select:
        break;
    }
}

void rv_editor_scene_frame(rv_editor_app &app, rv_editor_scene_camera &cam, bool all)
{
    rv_editor_scene_doc &doc = *app.scene;
    vec3 lo{ bounding_box_infinity, bounding_box_infinity, bounding_box_infinity },
        hi{ -bounding_box_infinity, -bounding_box_infinity, -bounding_box_infinity };
    // The selected object's drawn pieces frame alone; without them, its edges (or all objects for View All).
    if (!all && rv_editor_parts_bounds(doc, rv_editor_parts_resolve(doc, app.project), lo, hi)) {
        rv_editor_frame_box(cam, lo, hi);
        return;
    }
    bool any = false;
    for (size_t i = 0; i < doc.scene.objects.size(); ++i) {
        if (!all && doc.scene.objects[i].id != doc.selected) {
            continue;
        }
        if (rv_editor_scene_visibility(doc, doc.scene.objects[i]) == rv_editor_visibility::absent) {
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
    rv_editor_frame_box(cam, lo, hi);
}

// Scene toolbar, one row at 1280x720: View and the framing buttons, Hidden, Parts and Layers, then Shade.
void rv_editor_scene_toolbar(rv_editor_app &app, rv_editor_scene_camera &cam, const rv_editor_theme &theme)
{
    const float side = ImGui::GetFrameHeight();
    const char *no_selection = app.scene->selected.empty() ? rv_editor_text("scene_tools.nothing_selected") : nullptr;
    rv_editor_scene_view_select(cam, theme);
    rv_editor_flow(side);
    if (rv_editor_letter_button("##frame",
            rv_editor_glyph::frame,
            theme.code_blue,
            rv_editor_text("scene_tools.toolbar_frame_tooltip"),
            theme,
            { rv_editor_look::live, no_selection })) {
        rv_editor_scene_frame(app, cam, false);
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##view_all",
            rv_editor_glyph::view_all,
            theme.code_blue,
            rv_editor_text("scene_tools.toolbar_view_all"),
            theme)) {
        rv_editor_scene_frame(app, cam, true);
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##home",
            rv_editor_glyph::home,
            theme.code_blue,
            rv_editor_text("scene_tools.toolbar_home_tooltip"),
            theme)) {
        cam = rv_editor_scene_camera{ cam.tool, cam.snap, cam.snap_step, cam.grid, cam.shading };
    }
    rv_editor_flow(side);
    bool seek_val = cam.seeking;
    if (rv_editor_letter_toggle("##seek",
            rv_editor_glyph::seek,
            theme.code_blue,
            rv_editor_text("scene_tools.toolbar_seek_tooltip"),
            &seek_val,
            theme)) {
        cam.seeking = seek_val;
    }
    rv_editor_scene_group_gap();
    rv_editor_flow(side);
    bool hidden_val = app.scene->show_hidden;
    if (rv_editor_letter_toggle("##hidden",
            rv_editor_glyph::show_hidden,
            theme.code_green,
            rv_editor_text("scene_tools.toolbar_hidden_tooltip"),
            &hidden_val,
            theme)) {
        app.scene->show_hidden = hidden_val;
    }
    rv_editor_flow(side);
    bool parts_val = app.scene->parts_mode;
    if (rv_editor_letter_toggle("##parts",
            rv_editor_glyph::parts,
            theme.code_green,
            rv_editor_text("scene_tools.toolbar_parts_tooltip"),
            &parts_val,
            theme)) {
        app.scene->parts_mode = parts_val;
    }
    rv_editor_flow(side);
    if (rv_editor_letter_button("##layers",
            rv_editor_glyph::layers,
            theme.code_green,
            rv_editor_text("scene_tools.toolbar_layers_tooltip"),
            theme)) {
        ImGui::OpenPopup("##scene_layers");
    }
    rv_editor_scene_group_gap();
    rv_editor_scene_shading_select(cam, theme);
    // Last, so the popup's items never become the item rv_editor_flow measures.
    if (rv_editor_popup_begin("##scene_layers")) {
        rv_editor_scene_layers_menu(*app.scene, theme);
        rv_editor_popup_end();
    }
}

void rv_editor_scene_tools(const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    rv_editor_scene_camera cam = ui.state.scene_camera;
    const char *const tool_ids[] = { "scene_tools.tool_select",
        "scene_tools.tool_move",
        "scene_tools.tool_rotate",
        "scene_tools.tool_scale" };
    for (int t = 0; t < std::ssize(tool_ids); ++t) {
        const char *tool_name = rv_editor_text(tool_ids[t]);
        if (rv_editor_radio(tool_name, cam.tool == static_cast<rv_editor_scene_tool>(t), theme)) {
            cam.tool = static_cast<rv_editor_scene_tool>(t);
        }
    }
    const char *snap_label = rv_editor_text("scene_tools.snap_label");
    rv_editor_checkbox(snap_label, &cam.snap, theme);
    rv_editor_item_tooltip(rv_editor_text("scene_tools.snap_tooltip"));
    int step = default_snap_step_index;
    for (int i = 0; i < std::ssize(snap_step_values); ++i) {
        step = std::fabs(cam.snap_step - snap_step_values[i]) < snap_step_epsilon ? i : step;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * dropdown_width_factor);
    if (rv_editor_dropdown("##snapstep", &step, snap_step_labels, std::ssize(snap_step_labels), theme)) {
        cam.snap_step = snap_step_values[step];
    }
    rv_editor_item_tooltip(rv_editor_text("scene_tools.snap_step_tooltip"));
    const char *grid_label = rv_editor_text("scene_tools.grid_label");
    rv_editor_checkbox(grid_label, &cam.grid, theme);
    const bool changed = cam != ui.state.scene_camera;
    if (changed) {
        ui.requests.push_back({ .kind = rv_editor_ui_request_kind::set_scene_camera, .camera = cam });
    }
}

} // namespace rv_editor
