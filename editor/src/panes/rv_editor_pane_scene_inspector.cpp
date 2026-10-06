// Inspector: the selected scene object's values. A field's change is one undo
// step; a drag is one step from press to release and Escape takes it back; a name
// or a resource is committed on Enter or on leaving the field.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Field labels end this many font sizes in.
constexpr float label_column_em = 6.0f;

// Largest tint channel value.
constexpr int tint_channel_max = 255;

// Components in a 3D vector (position, rotation, scale).
constexpr int vec3_size = 3;

// Minimum tessellation value clamped in drag.
constexpr double tess_min = 0.01;

// Drag speeds for transform fields.
constexpr float position_speed = 0.01f;
constexpr float rotation_speed = 0.5f;
constexpr float scale_speed = 0.01f;
constexpr float uv_speed = 0.5f;
constexpr float tess_speed = 0.05f;

// Scene object kinds: category for inspector layout and properties.
constexpr std::string_view kind_mesh = "mesh";
constexpr std::string_view kind_quad = "quad";
constexpr std::string_view kind_billboard = "billboard";

// UV drag range for coordinate edit fields.
constexpr double uv_drag_bound = 1e6;

// Undo step names for inspector fields.
constexpr const char *undo_name = "Name";
constexpr const char *undo_position = "Position";
constexpr const char *undo_rotation = "Rotation";
constexpr const char *undo_scale = "Scale";
constexpr const char *undo_mesh = "Mesh";
constexpr const char *undo_texture = "Texture";
constexpr const char *undo_uv = "UV";
constexpr const char *undo_tint = "Tint";
constexpr const char *undo_tess = "Tess";

// A label column, then the field.
void rv_editor_inspector_label(const char *label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetFontSize() * label_column_em);
    ImGui::SetNextItemWidth(-1.0f);
}

// Three numbers dragged or typed. The press takes one undo step and remembers the
// values; Escape during the drag puts them back and drops that step.
void rv_editor_inspector_vec(rv_editor_app &app, const char *label, rv_editor_vec3 rv_editor_scene_object::*field,
    float speed, const std::string &id, const char *display_label)
{
    rv_editor_scene_doc &doc = *app.scene;
    rv_editor_scene_ui &ui = app.scene_ui;
    const int at = rv_editor_scene_find(doc.scene, id);
    if (at < 0) {
        return;
    }
    rv_editor_vec3 value = doc.scene.objects[static_cast<size_t>(at)].*field;
    rv_editor_inspector_label(display_label);
    const bool changed = ImGui::DragScalarN((std::string("##") + label).c_str(), ImGuiDataType_Double, value.data(), vec3_size,
        speed, nullptr, nullptr, "%.3f");
    if (ImGui::IsItemActivated()) {
        rv_editor_scene_step(doc);
        ui.editing = label;
        ui.before = doc.scene.objects[static_cast<size_t>(at)].*field;
    }
    rv_editor_vec3 &target = doc.scene.objects[static_cast<size_t>(at)].*field;
    const bool mine = ui.editing == label;
    // ImGui lets go of the field on Escape itself, so the key is also caught as it does.
    const bool escaped = ImGui::IsKeyPressed(ImGuiKey_Escape) && (ImGui::IsItemActive() || ImGui::IsItemDeactivated());
    // A press that changed nothing, or Escape: the step taken at the press goes again.
    const bool idle = ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit();
    if (mine && !ui.cancelled && (escaped || idle)) {
        ui.cancelled = escaped;
        if (!doc.undo.empty()) {
            doc.undo.pop_back();
        }
        if (!escaped) {
            ui.editing.clear();
        }
    }
    // Taken back: the old values hold until the button is let go.
    if (mine && ui.cancelled) {
        target = ui.before;
        if (!ImGui::IsItemActive()) {
            ui.cancelled = false;
            ui.editing.clear();
        }
        return;
    }
    if (changed) {
        target = value;
    }
}

// A fixed-size numeric array dragged or typed (uv, tint), the same undo and
// Escape rule as rv_editor_inspector_vec above. `before` holds the array's own
// type so uv (4 doubles) and tint (3 ints) each keep their own memory of it.
template <typename Arr>
void rv_editor_inspector_array(rv_editor_app &app, const char *label, Arr rv_editor_scene_object::*field,
    ImGuiDataType type, float speed, const char *fmt, const std::string &id,
    typename Arr::value_type lo, typename Arr::value_type hi, const char *display_label)
{
    static Arr before{};
    rv_editor_scene_doc &doc = *app.scene;
    rv_editor_scene_ui &ui = app.scene_ui;
    const int at = rv_editor_scene_find(doc.scene, id);
    if (at < 0) {
        return;
    }
    Arr value = doc.scene.objects[static_cast<size_t>(at)].*field;
    rv_editor_inspector_label(display_label);
    const bool changed = ImGui::DragScalarN((std::string("##") + label).c_str(), type, value.data(),
        static_cast<int>(value.size()), speed, &lo, &hi, fmt);
    if (ImGui::IsItemActivated()) {
        rv_editor_scene_step(doc);
        ui.editing = label;
        before = doc.scene.objects[static_cast<size_t>(at)].*field;
    }
    Arr &target = doc.scene.objects[static_cast<size_t>(at)].*field;
    const bool mine = ui.editing == label;
    const bool escaped = ImGui::IsKeyPressed(ImGuiKey_Escape) && (ImGui::IsItemActive() || ImGui::IsItemDeactivated());
    const bool idle = ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit();
    if (mine && !ui.cancelled && (escaped || idle)) {
        ui.cancelled = escaped;
        if (!doc.undo.empty()) {
            doc.undo.pop_back();
        }
        if (!escaped) {
            ui.editing.clear();
        }
    }
    if (mine && ui.cancelled) {
        target = before;
        if (!ImGui::IsItemActive()) {
            ui.cancelled = false;
            ui.editing.clear();
        }
        return;
    }
    if (changed) {
        target = value;
    }
}

// A single number dragged or typed (tess), clamped to stay above zero.
void rv_editor_inspector_tess(rv_editor_app &app, const char *label, double rv_editor_scene_object::*field,
    float speed, const std::string &id, const char *display_label)
{
    static double before = 0.0;
    rv_editor_scene_doc &doc = *app.scene;
    rv_editor_scene_ui &ui = app.scene_ui;
    const int at = rv_editor_scene_find(doc.scene, id);
    if (at < 0) {
        return;
    }
    double value = doc.scene.objects[static_cast<size_t>(at)].*field;
    rv_editor_inspector_label(display_label);
    const double lo = tess_min;
    const bool changed =
        ImGui::DragScalar((std::string("##") + label).c_str(), ImGuiDataType_Double, &value, speed, &lo, nullptr, "%.3f");
    if (ImGui::IsItemActivated()) {
        rv_editor_scene_step(doc);
        ui.editing = label;
        before = doc.scene.objects[static_cast<size_t>(at)].*field;
    }
    double &target = doc.scene.objects[static_cast<size_t>(at)].*field;
    const bool mine = ui.editing == label;
    const bool escaped = ImGui::IsKeyPressed(ImGuiKey_Escape) && (ImGui::IsItemActive() || ImGui::IsItemDeactivated());
    const bool idle = ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit();
    if (mine && !ui.cancelled && (escaped || idle)) {
        ui.cancelled = escaped;
        if (!doc.undo.empty()) {
            doc.undo.pop_back();
        }
        if (!escaped) {
            ui.editing.clear();
        }
    }
    if (mine && ui.cancelled) {
        target = before;
        if (!ImGui::IsItemActive()) {
            ui.cancelled = false;
            ui.editing.clear();
        }
        return;
    }
    if (changed) {
        target = std::max(value, lo);
    }
}

// A text value committed as a whole, never letter by letter.
void rv_editor_inspector_text(rv_editor_app &app, const char *label, std::string rv_editor_scene_object::*field,
    char *buf, size_t size, const std::string &id, const rv_editor_theme &theme, const char *display_label)
{
    rv_editor_scene_doc &doc = *app.scene;
    const int at = rv_editor_scene_find(doc.scene, id);
    rv_editor_inspector_label(display_label);
    rv_editor_text_field((std::string("##") + label).c_str(), buf, size, theme);
    const bool commit = ImGui::IsItemDeactivatedAfterEdit();
    if (at < 0 || !commit) {
        return;
    }
    const std::string value(buf);
    if (value != doc.scene.objects[static_cast<size_t>(at)].*field) {
        rv_editor_scene_step(doc);
        doc.scene.objects[static_cast<size_t>(at)].*field = value;
    }
}

} // namespace

void rv_editor_pane_scene_inspector(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    rv_editor_scene_keys(app);
    // Content only, no controls row: the whole pane is one well. Early returns
    // inside stay reachable, so the body is a lambda that always runs to well_end.
    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const auto body = [&]() {
        if (app.scene == nullptr) {
            ImGui::TextWrapped("%s", rv_editor_text("pane_scene_inspector.no_scene_open"));
            rv_editor_scene_open_row(app, theme);
            return;
        }
        rv_editor_scene_doc &doc = *app.scene;
        rv_editor_scene_ui &ui = app.scene_ui;
        const int at = rv_editor_scene_find(doc.scene, doc.selected);
        if (at < 0) {
            ImGui::TextWrapped("%s", rv_editor_text("pane_scene_inspector.select_object"));
            return;
        }
        const rv_editor_scene_object o = doc.scene.objects[static_cast<size_t>(at)];
        // The text fields follow the selection and undo; the one being typed in keeps its text.
        if (ui.shown != o.id || !ImGui::GetIO().WantTextInput) {
            std::snprintf(ui.name, sizeof(ui.name), "%s", o.name.c_str());
            std::snprintf(ui.mesh, sizeof(ui.mesh), "%s", o.mesh.c_str());
            std::snprintf(ui.texture, sizeof(ui.texture), "%s", o.texture.c_str());
            ui.shown = o.id;
        }
        const bool read_only = !doc.scene.read_only.empty();
        if (read_only) {
            const std::string &ro = doc.scene.read_only;
            const auto msg = rv_editor_text_format("pane_scene_inspector.read_only",
                std::make_format_args(ro));
            ImGui::TextWrapped("%s", msg.c_str());
        }
        ImGui::BeginDisabled(read_only);
        rv_editor_inspector_text(app, undo_name, &rv_editor_scene_object::name, ui.name, sizeof(ui.name), o.id, theme,
            rv_editor_text("pane_scene_inspector.name_label"));
        rv_editor_inspector_label(rv_editor_text("pane_scene_inspector.kind_label"));
        ImGui::TextUnformatted(o.kind == kind_mesh ? rv_editor_text("pane_scene_inspector.kind_mesh") :
                                                     o.kind.c_str());
        rv_editor_inspector_label(rv_editor_text("pane_scene_inspector.id_label"));
        ImGui::TextDisabled("%s", o.id.c_str());
        ImGui::SeparatorText(rv_editor_text("pane_scene_inspector.transform_header"));
        rv_editor_inspector_vec(app, undo_position, &rv_editor_scene_object::position, position_speed, o.id,
            rv_editor_text("pane_scene_inspector.position_label"));
        rv_editor_inspector_vec(app, undo_rotation, &rv_editor_scene_object::rotation, rotation_speed, o.id,
            rv_editor_text("pane_scene_inspector.rotation_label"));
        ImGui::SetItemTooltip("%s", rv_editor_text("pane_scene_inspector.rotation_tooltip"));
        rv_editor_inspector_vec(app, undo_scale, &rv_editor_scene_object::scale, scale_speed, o.id,
            rv_editor_text("pane_scene_inspector.scale_label"));
        if (o.kind == kind_mesh) {
            ImGui::SeparatorText(rv_editor_text("pane_scene_inspector.resources_header"));
            rv_editor_inspector_text(app, undo_mesh, &rv_editor_scene_object::mesh, ui.mesh, sizeof(ui.mesh), o.id, theme,
                rv_editor_text("pane_scene_inspector.mesh_label"));
            ImGui::SetItemTooltip("%s", rv_editor_text("pane_scene_inspector.mesh_tooltip"));
            rv_editor_inspector_text(app, undo_texture, &rv_editor_scene_object::texture, ui.texture, sizeof(ui.texture),
                o.id, theme, rv_editor_text("pane_scene_inspector.texture_label"));
            ImGui::SetItemTooltip("%s", rv_editor_text("pane_scene_inspector.texture_tooltip"));
        }
        if (o.kind == kind_quad || o.kind == kind_billboard) {
            ImGui::SeparatorText(rv_editor_text("pane_scene_inspector.resources_header"));
            rv_editor_inspector_text(app, undo_texture, &rv_editor_scene_object::texture, ui.texture, sizeof(ui.texture),
                o.id, theme, rv_editor_text("pane_scene_inspector.texture_label"));
            ImGui::SetItemTooltip("%s", rv_editor_text("pane_scene_inspector.texture_tooltip"));
            rv_editor_inspector_array<rv_editor_uv>(app, undo_uv, &rv_editor_scene_object::uv, ImGuiDataType_Double, uv_speed,
                "%.1f", o.id, -uv_drag_bound, uv_drag_bound, rv_editor_text("pane_scene_inspector.uv_label"));
            ImGui::SetItemTooltip("%s", rv_editor_text("pane_scene_inspector.uv_tooltip"));
            rv_editor_inspector_array<rv_editor_tint>(app, undo_tint, &rv_editor_scene_object::tint, ImGuiDataType_S32,
                1.0f, "%d", o.id, 0, tint_channel_max, rv_editor_text("pane_scene_inspector.tint_label"));
            rv_editor_inspector_tess(app, undo_tess, &rv_editor_scene_object::tess, tess_speed, o.id,
                rv_editor_text("pane_scene_inspector.tess_label"));
            ImGui::SetItemTooltip("%s", rv_editor_text("pane_scene_inspector.tess_tooltip"));
        }
        if (!o.extra.empty()) {
            ImGui::SeparatorText(rv_editor_text("pane_scene_inspector.extra_header"));
            for (const auto &e : o.extra) {
                ImGui::TextDisabled("%s", e.key.c_str());
            }
            ImGui::SetItemTooltip("%s", rv_editor_text("pane_scene_inspector.extra_tooltip"));
        }
        ImGui::EndDisabled();
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
