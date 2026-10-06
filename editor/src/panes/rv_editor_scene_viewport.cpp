// The Scene viewport: the document drawn in wireframe
// through the editor's own camera, which is not the game's. Click picks, the tool
// in the Toolchest moves, turns or scales the selected object, one undo step per
// drag with Escape taking it back; the thumbwheels, the mouse and the keys orbit,
// pan and dolly the view.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "imgui.h"

#include "panes/rv_editor_scene_draw.hpp"
#include "scene/rv_editor_scene.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_thumbwheel.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

constexpr float wheel_height_factor = 0.75f;
constexpr float min_viewport_size = 40.0f;
constexpr float wheel_width_spacing = 2.0f;
constexpr float line_height_spacing = 2.0f;
constexpr float gap_multiplier = 4.0f;
constexpr double rotation_sensitivity = 0.4;
constexpr double pitch_min = -89.9;
constexpr double pitch_max = 89.9;
constexpr float half = 0.5f;
constexpr double dolly_sensitivity = 0.01;
constexpr double camera_distance_min = 0.5;
constexpr double camera_distance_max = 200.0;
constexpr double pan_sensitivity = 0.002;
constexpr double zoom_base = 0.9;
constexpr double keyboard_rotation_step = 2.0;
constexpr float line_width_selected = 2.0f;
constexpr float gizmo_line_width_normal = 2.0f;
constexpr float gizmo_line_width_hot = 4.0f;
constexpr float gizmo_circle_radius = 40.0f;
constexpr int gizmo_circle_segments = 32;
constexpr float gizmo_circle_line_width = 1.5f;
constexpr double gizmo_size_factor = 0.15;
constexpr float gizmo_click_threshold = 40.0f;
constexpr float text_wrap_width = 28.0f;
constexpr float status_text_offset = 2.0f;
constexpr int grid_half_extent = 10;
constexpr size_t camera_status_buffer_size = 160; // the camera status line: yaw, pitch, distance

} // namespace

void rv_editor_scene_viewport(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme)
{
    rv_editor_scene_doc &doc = *app.scene;
    rv_editor_scene_ui &ui = app.scene_ui;
    rv_editor_scene_camera &cam = ui.camera;
    rv_editor_shelf_begin("##shelf", theme);
    rv_editor_scene_toolbar(app, theme);
    rv_editor_shelf_end();
    rv_editor_scene_keys(app);

    // The well never scrolls: the Scene tile's picture shrinks to fit instead.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // The canvas between a Rot X wheel on the left, a Dolly wheel on the right and a
    // Rot Y wheel below, as Open Inventor's viewers have them; each wheel carries a
    // visible label in the UI font.
    const char *const rot_x_label = rv_editor_text("scene_viewport.rot_x");
    const char *const rot_y_label = rv_editor_text("scene_viewport.rot_y");
    const char *const dolly_label = rv_editor_text("scene_viewport.dolly");
    const float wheel = ImGui::GetFrameHeight() * wheel_height_factor;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 label_x = ImGui::CalcTextSize(rot_x_label);
    const ImVec2 label_y = ImGui::CalcTextSize(rot_y_label);
    const ImVec2 label_d = ImGui::CalcTextSize(dolly_label);
    const ImVec2 size(
        std::max(min_viewport_size, avail.x - wheel_width_spacing * wheel - label_x.x - label_d.x - gap_multiplier * gap),
        std::max(min_viewport_size, avail.y - wheel - line_height_spacing * line - gap));
    bool reset = false;
    const ImVec2 top = ImGui::GetCursorScreenPos();
    cam.pitch = std::clamp(
        cam.pitch - rv_editor_thumbwheel("##rotx", rot_x_label, true, size.y, theme, reset) * rotation_sensitivity,
        pitch_min, pitch_max);
    if (reset) {
        cam.pitch = rv_editor_scene_camera{}.pitch;
    }
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, top.y + (size.y - label_x.y) * half));
    ImGui::TextUnformatted(rot_x_label);
    ImGui::SameLine();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##viewport", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused() || ImGui::IsWindowFocused();
    ImGui::SameLine();
    const ImVec2 dolly_top = ImGui::GetCursorScreenPos();
    cam.distance = std::clamp(
        cam.distance * std::exp(-rv_editor_thumbwheel("##dolly", dolly_label, true, size.y, theme, reset) * dolly_sensitivity),
        camera_distance_min, camera_distance_max);
    if (reset) {
        cam.distance = rv_editor_scene_camera{}.distance;
    }
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, dolly_top.y + (size.y - label_d.y) * half));
    ImGui::TextUnformatted(dolly_label);
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + size.y + gap));
    cam.yaw += rv_editor_thumbwheel("##roty", rot_y_label, false, size.x - gap - label_y.x, theme,
                   reset) *
        rotation_sensitivity;
    if (reset) {
        cam.yaw = rv_editor_scene_camera{}.yaw;
    }
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, p0.y + size.y + gap + (wheel - label_y.y) * half));
    ImGui::TextUnformatted(rot_y_label);

    const rv_editor_view v = rv_editor_view_make(cam, p0, size);
    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const double gizmo = cam.distance * gizmo_size_factor;
    const bool read_only = !doc.scene.read_only.empty();

    // Navigation: right drag orbits, middle drag pans, the wheel dollies; keys as well.
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        cam.yaw += io.MouseDelta.x * rotation_sensitivity;
        cam.pitch = std::clamp(cam.pitch + io.MouseDelta.y * rotation_sensitivity, pitch_min, pitch_max);
    }
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        const double k = cam.distance * pan_sensitivity;
        cam.target = add(cam.target, add(mul(v.right, -io.MouseDelta.x * k), mul(v.up, io.MouseDelta.y * k)));
    }
    if (hovered && io.MouseWheel != 0.0f) {
        cam.distance = std::clamp(cam.distance * std::pow(zoom_base, io.MouseWheel), camera_distance_min, camera_distance_max);
    }
    if (focused && !io.WantTextInput && !ImGui::IsAnyItemActive()) {
        cam.yaw += ImGui::IsKeyDown(ImGuiKey_LeftArrow) ? -keyboard_rotation_step :
            ImGui::IsKeyDown(ImGuiKey_RightArrow)       ? keyboard_rotation_step :
                                                          0.0;
        cam.pitch = std::clamp(cam.pitch + (ImGui::IsKeyDown(ImGuiKey_UpArrow) ? keyboard_rotation_step : 0.0) -
                (ImGui::IsKeyDown(ImGuiKey_DownArrow) ? keyboard_rotation_step : 0.0),
            pitch_min, pitch_max);
        if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
            cam.distance = std::max(camera_distance_min, cam.distance * zoom_base);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
            cam.distance = std::min(camera_distance_max, cam.distance / zoom_base);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            cam = rv_editor_scene_camera{ cam.tool, cam.snap, cam.snap_step, cam.grid, cam.shading };
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F) && !doc.selected.empty()) {
            rv_editor_scene_frame(app, false);
        }
        // One letter per tool, only here, where it is not typing.
        const ImGuiKey keys[] = { ImGuiKey_Q, ImGuiKey_W, ImGuiKey_E, ImGuiKey_R };
        for (int t = 0; t < std::ssize(keys); ++t) {
            if (ImGui::IsKeyPressed(keys[t], false)) {
                cam.tool = static_cast<rv_editor_scene_tool>(t);
            }
        }
    }

    // A left press starts a drag of the tool when it lands on the gizmo, else it picks.
    if (ImGui::IsItemActivated() || (active && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
        const int at = rv_editor_scene_find(doc.scene, doc.selected);
        int axis = -1;
        if (at >= 0 && !read_only && !cam.seeking) {
            if (cam.tool == rv_editor_scene_tool::move) {
                axis = rv_editor_gizmo_axis(doc.scene, doc.selected, v, mouse, gizmo);
            } else if (cam.tool != rv_editor_scene_tool::select) {
                ImVec2 so;
                const vec3 o = rv_editor_affine_point(rv_editor_scene_world(doc.scene, at), { 0, 0, 0 });
                if (v.point(o, so) && std::hypot(mouse.x - so.x, mouse.y - so.y) < gizmo_click_threshold) {
                    axis = 0;
                }
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && axis >= 0) {
            rv_editor_scene_step(doc);
            const rv_editor_scene_object &o = doc.scene.objects[static_cast<size_t>(at)];
            ui.gizmo_axis = axis;
            ui.gizmo_from = { mouse.x, mouse.y };
            ui.gizmo_before_position = o.position;
            ui.gizmo_before_rotation = o.rotation;
            ui.gizmo_before_scale = o.scale;
            ui.gizmo_dragging = true;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const std::string hit = rv_editor_pick(doc.scene, app.project, v, mouse);
            if (cam.seeking) {
                const int h = rv_editor_scene_find(doc.scene, hit);
                if (h >= 0) {
                    cam.target = rv_editor_affine_point(rv_editor_scene_world(doc.scene, h), { 0, 0, 0 });
                }
                cam.seeking = false;
            } else {
                doc.selected = hit;
            }
        }
    }
    if (ui.gizmo_dragging) {
        const int at = rv_editor_scene_find(doc.scene, doc.selected);
        if (at >= 0 && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            // Taken back: the values from before the press, and the step it took goes.
            rv_editor_scene_object &o = doc.scene.objects[static_cast<size_t>(at)];
            o.position = ui.gizmo_before_position;
            o.rotation = ui.gizmo_before_rotation;
            o.scale = ui.gizmo_before_scale;
            if (!doc.undo.empty()) {
                doc.undo.pop_back();
            }
            ui.gizmo_dragging = false;
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            rv_editor_manipulate(app, v);
        } else {
            ui.gizmo_dragging = false;
        }
    }

    // The picture: dark ground, the grid on y = 0, the objects, the gizmo.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    dl->PushClipRect(p0, p1, true);
    dl->AddRectFilled(p0, p1, rv_editor_col(theme.code_base));
    if (cam.grid) {
        const ImU32 grid = rv_editor_col(theme.dark);
        for (int i = -grid_half_extent; i <= grid_half_extent; ++i) {
            const double f = static_cast<double>(i);
            rv_editor_line(dl, v, { f, 0, -grid_half_extent }, { f, 0, grid_half_extent },
                i == 0 ? rv_editor_col(theme.code_blue) : grid, 1.0f);
            rv_editor_line(dl, v, { -grid_half_extent, 0, f }, { grid_half_extent, 0, f },
                i == 0 ? rv_editor_col(theme.code_red) : grid, 1.0f);
        }
    }
    const bool filled = cam.shading != rv_editor_scene_shading::wireframe;
    std::string mesh_error; // the first mesh resolve/parse error this frame, if any
    if (filled) {
        rv_editor_draw_filled(dl, v, doc.scene, app.project, doc.selected, rv_editor_col(theme.code_subtext),
            rv_editor_col(theme.selection), cam.shading == rv_editor_scene_shading::textured ? renderer : nullptr);
    }
    for (size_t i = 0; i < doc.scene.objects.size(); ++i) {
        const std::string &kind = doc.scene.objects[i].kind;
        const bool sel = doc.scene.objects[i].id == doc.selected;
        const ImU32 color = rv_editor_col(sel ? theme.selection : kind == kind_mesh || kind == kind_quad || kind == kind_billboard ? theme.text :
                kind == kind_camera                                                                                                ? theme.code_yellow :
                                                                                                                                     theme.code_subtext);
        std::string error;
        // Filled/Textured mode already paints non-selected mesh/quad/billboard geometry; the
        // selected one keeps its outline, and a volume (never filled) always keeps its edges.
        const bool painted = filled && !sel && (kind == kind_mesh || kind == kind_quad || kind == kind_billboard);
        if (painted) {
            if (kind == kind_mesh) {
                rv_editor_object_triangles(doc.scene, app.project, static_cast<int>(i), &error);
            }
        } else {
            for (const auto &[a, b] : rv_editor_object_edges(doc.scene, app.project, static_cast<int>(i), &error)) {
                rv_editor_line(dl, v, a, b, color, sel ? line_width_selected : 1.0f);
            }
        }
        if (mesh_error.empty() && !error.empty()) {
            mesh_error = error;
        }
    }
    const int at = rv_editor_scene_find(doc.scene, doc.selected);
    if (at >= 0 && cam.tool == rv_editor_scene_tool::move) {
        const vec3 o = rv_editor_affine_point(rv_editor_scene_world(doc.scene, at), { 0, 0, 0 });
        const uint32_t colors[] = { theme.code_red, theme.code_green, theme.code_blue };
        for (int a = 0; a < std::ssize(colors); ++a) {
            vec3 tip = o;
            tip[static_cast<size_t>(a)] += gizmo;
            const bool hot = (ui.gizmo_dragging && ui.gizmo_axis == a) ||
                (!ui.gizmo_dragging && hovered && rv_editor_gizmo_axis(doc.scene, doc.selected, v, mouse, gizmo) == a);
            rv_editor_line(dl, v, o, tip, rv_editor_col(colors[a]), hot ? gizmo_line_width_hot : gizmo_line_width_normal);
        }
    } else if (at >= 0 && cam.tool != rv_editor_scene_tool::select) {
        ImVec2 so;
        if (v.point(rv_editor_affine_point(rv_editor_scene_world(doc.scene, at), { 0, 0, 0 }), so)) {
            dl->AddCircle(so, gizmo_circle_radius, rv_editor_col(theme.selection),
                gizmo_circle_segments, gizmo_circle_line_width);
        }
    }
    dl->PopClipRect();

    // What the view is, in numbers, and which camera this is.
    const std::string seek_msg = cam.seeking ? rv_editor_text("scene_viewport.seek_hint") : "";
    const std::string camera_info = rv_editor_text_format("scene_viewport.camera_status",
        std::make_format_args(cam.yaw, cam.pitch, cam.distance, seek_msg));
    char buf[camera_status_buffer_size];
    std::snprintf(buf, sizeof(buf), "%s", camera_info.c_str());
    // Whether the running game has read this open scene document (its resource
    // name is the file's own name: the disc flattens scenes/*.scene.toml). The
    // status line stays short; the reason is a tooltip.
    const std::string scene_name = doc.scene.path.filename().string();
    const char *read_line = rv_editor_text("scene_viewport.game_not_running");
    std::string read_tip;
    if (app.session.connected()) {
        if (app.session.scenes_read().contains(scene_name)) {
            read_line = rv_editor_text("scene_viewport.game_read_scene");
            read_tip = rv_editor_text("scene_viewport.game_restart_needed");
        } else {
            read_line = rv_editor_text("scene_viewport.game_not_read_scene");
            read_tip = rv_editor_text_format("scene_viewport.game_disc_not_opened",
                std::make_format_args(scene_name));
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + size.y + gap + wheel + status_text_offset));
    if (mesh_error.empty()) {
        const std::string status_line = rv_editor_text_format("scene_viewport.status_line_format",
            std::make_format_args(buf, read_line));
        ImGui::TextDisabled("%s", status_line.c_str());
    } else {
        const char *error_str = mesh_error.c_str();
        const std::string status_line = rv_editor_text_format("scene_viewport.mesh_placeholder_format",
            std::make_format_args(buf, read_line, error_str));
        ImGui::TextDisabled("%s", status_line.c_str());
    }
    if (!read_tip.empty()) {
        ImGui::SetItemTooltip("%s", read_tip.c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + size.y + gap + wheel + status_text_offset + line));
    ImGui::TextDisabled("%s", rv_editor_text("scene_viewport.example_meshes_title"));
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * text_wrap_width);
        ImGui::TextUnformatted(rv_editor_text("scene_viewport.example_meshes_tooltip"));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    rv_editor_well_end();
}

} // namespace rv_editor
