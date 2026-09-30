// The Scene viewport (SCN-05, SCN-06, SCL-02): the document drawn in wireframe
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
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_thumbwheel.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

double rv_editor_snap(double v, double step)
{
    return std::round(v / step) * step;
}

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
        cam = rv_editor_scene_camera{ cam.tool, cam.snap, cam.snap_step, cam.grid };
    }
    ImGui::SetItemTooltip("Home key: the view the scene opened with");
    rv_editor_flow(rv_editor_button_width("Seek"));
    bool seek = cam.seeking;
    if (rv_editor_toggle("Seek", &seek, theme)) {
        cam.seeking = seek;
    }
    ImGui::SetItemTooltip("Then click an object: the view turns about it");
}

} // namespace

void rv_editor_scene_viewport(rv_editor_app &app, const rv_editor_theme &theme)
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
    const float wheel = ImGui::GetFrameHeight() * 0.75f;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 label_x = ImGui::CalcTextSize("Rot X");
    const ImVec2 label_y = ImGui::CalcTextSize("Rot Y");
    const ImVec2 label_d = ImGui::CalcTextSize("Dolly");
    const ImVec2 size(std::max(40.0f, avail.x - 2.0f * wheel - label_x.x - label_d.x - 4.0f * gap),
        std::max(40.0f, avail.y - wheel - line - gap));
    bool reset = false;
    const ImVec2 top = ImGui::GetCursorScreenPos();
    cam.pitch = std::clamp(cam.pitch - rv_editor_thumbwheel("##rotx", "Rot X", true, size.y, theme, reset) * 0.4,
        -89.9, 89.9);
    if (reset) {
        cam.pitch = rv_editor_scene_camera{}.pitch;
    }
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, top.y + (size.y - label_x.y) * 0.5f));
    ImGui::TextUnformatted("Rot X");
    ImGui::SameLine();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##viewport", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused() || ImGui::IsWindowFocused();
    ImGui::SameLine();
    const ImVec2 dolly_top = ImGui::GetCursorScreenPos();
    cam.distance = std::clamp(cam.distance * std::exp(-rv_editor_thumbwheel("##dolly", "Dolly", true, size.y, theme, reset) *
        0.01), 0.5, 200.0);
    if (reset) {
        cam.distance = rv_editor_scene_camera{}.distance;
    }
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, dolly_top.y + (size.y - label_d.y) * 0.5f));
    ImGui::TextUnformatted("Dolly");
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + size.y + gap));
    cam.yaw += rv_editor_thumbwheel("##roty", "Rot Y", false, size.x - gap - label_y.x, theme, reset) * 0.4;
    if (reset) {
        cam.yaw = rv_editor_scene_camera{}.yaw;
    }
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, p0.y + size.y + gap + (wheel - label_y.y) * 0.5f));
    ImGui::TextUnformatted("Rot Y");

    const rv_editor_view v = rv_editor_view_make(cam, p0, size);
    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const double gizmo = cam.distance * 0.15;
    const bool read_only = !doc.scene.read_only.empty();

    // Navigation: right drag orbits, middle drag pans, the wheel dollies; keys as well.
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        cam.yaw += io.MouseDelta.x * 0.4;
        cam.pitch = std::clamp(cam.pitch + io.MouseDelta.y * 0.4, -89.9, 89.9);
    }
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        const double k = cam.distance * 0.002;
        cam.target = add(cam.target, add(mul(v.right, -io.MouseDelta.x * k), mul(v.up, io.MouseDelta.y * k)));
    }
    if (hovered && io.MouseWheel != 0.0f) {
        cam.distance = std::clamp(cam.distance * std::pow(0.9, io.MouseWheel), 0.5, 200.0);
    }
    if (focused && !io.WantTextInput && !ImGui::IsAnyItemActive()) {
        cam.yaw += ImGui::IsKeyDown(ImGuiKey_LeftArrow) ? -2.0 : ImGui::IsKeyDown(ImGuiKey_RightArrow) ? 2.0 : 0.0;
        cam.pitch = std::clamp(cam.pitch + (ImGui::IsKeyDown(ImGuiKey_UpArrow) ? 2.0 : 0.0) -
            (ImGui::IsKeyDown(ImGuiKey_DownArrow) ? 2.0 : 0.0), -89.9, 89.9);
        if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
            cam.distance = std::max(0.5, cam.distance * 0.9);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
            cam.distance = std::min(200.0, cam.distance / 0.9);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            cam = rv_editor_scene_camera{ cam.tool, cam.snap, cam.snap_step, cam.grid };
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F) && !doc.selected.empty()) {
            rv_editor_scene_frame(app, false);
        }
        // One letter per tool, only here, where it is not typing (CMD-03).
        const ImGuiKey keys[] = { ImGuiKey_Q, ImGuiKey_W, ImGuiKey_E, ImGuiKey_R };
        for (int t = 0; t < 4; ++t) {
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
                if (v.point(o, so) && std::hypot(mouse.x - so.x, mouse.y - so.y) < 40.0f) {
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
        for (int i = -10; i <= 10; ++i) {
            const double f = static_cast<double>(i);
            rv_editor_line(dl, v, { f, 0, -10 }, { f, 0, 10 }, i == 0 ? rv_editor_col(theme.code_blue) : grid, 1.0f);
            rv_editor_line(dl, v, { -10, 0, f }, { 10, 0, f }, i == 0 ? rv_editor_col(theme.code_red) : grid, 1.0f);
        }
    }
    std::string mesh_error; // the first mesh resolve/parse error this frame, if any
    for (size_t i = 0; i < doc.scene.objects.size(); ++i) {
        const std::string &kind = doc.scene.objects[i].kind;
        const bool sel = doc.scene.objects[i].id == doc.selected;
        const ImU32 color = rv_editor_col(sel ? theme.selection
                : kind == "mesh"                                       ? theme.text
                : kind == "camera"                                     ? theme.code_yellow
                                                                        : theme.code_subtext);
        std::string error;
        for (const auto &[a, b] : rv_editor_object_edges(doc.scene, app.project, static_cast<int>(i), &error)) {
            rv_editor_line(dl, v, a, b, color, sel ? 2.0f : 1.0f);
        }
        if (mesh_error.empty() && !error.empty()) {
            mesh_error = error;
        }
    }
    const int at = rv_editor_scene_find(doc.scene, doc.selected);
    if (at >= 0 && cam.tool == rv_editor_scene_tool::move) {
        const vec3 o = rv_editor_affine_point(rv_editor_scene_world(doc.scene, at), { 0, 0, 0 });
        const uint32_t colors[3] = { theme.code_red, theme.code_green, theme.code_blue };
        for (int a = 0; a < 3; ++a) {
            vec3 tip = o;
            tip[static_cast<size_t>(a)] += gizmo;
            const bool hot = (ui.gizmo_dragging && ui.gizmo_axis == a) ||
                (!ui.gizmo_dragging && hovered && rv_editor_gizmo_axis(doc.scene, doc.selected, v, mouse, gizmo) == a);
            rv_editor_line(dl, v, o, tip, rv_editor_col(colors[a]), hot ? 4.0f : 2.0f);
        }
    } else if (at >= 0 && cam.tool != rv_editor_scene_tool::select) {
        ImVec2 so;
        if (v.point(rv_editor_affine_point(rv_editor_scene_world(doc.scene, at), { 0, 0, 0 }), so)) {
            dl->AddCircle(so, 40.0f, rv_editor_col(theme.selection), 32, 1.5f);
        }
    }
    dl->PopClipRect();

    // What the view is, in numbers (SCN-05), and which camera this is.
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Editor camera, not the game's: yaw %.0f, pitch %.0f, distance %.1f%s", cam.yaw,
        cam.pitch, cam.distance, cam.seeking ? " | Seek: click an object" : "");
    // Whether the running game has read this open scene document (its resource
    // name is the file's own name: the disc flattens scenes/*.scene.toml). The
    // status line stays short; the reason is a tooltip.
    const std::string scene_name = doc.scene.path.filename().string();
    const char *read_line = "Game: not running";
    std::string read_tip;
    if (app.session.connected()) {
        if (app.session.scenes_read().contains(scene_name)) {
            read_line = "Game: read this scene";
            read_tip = "Restart the game after Save and Build to see edits";
        } else {
            read_line = "Game: has not read this scene";
            read_tip = "The running disc did not open " + scene_name;
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + size.y + gap + wheel + 2.0f));
    if (mesh_error.empty()) {
        ImGui::TextDisabled("%s | %s", buf, read_line);
    } else {
        ImGui::TextDisabled("%s | %s | Mesh placeholder: %s", buf, read_line, mesh_error.c_str());
    }
    if (!read_tip.empty()) {
        ImGui::SetItemTooltip("%s", read_tip.c_str());
    }
    rv_editor_well_end();
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
