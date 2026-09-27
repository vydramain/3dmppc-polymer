// Inspector: the selected scene object's values. A field's change is one undo
// step; a drag is one step from press to release and Escape takes it back; a name
// or a resource is committed on Enter or on leaving the field (SCN-03).

#include "panes/rv_editor_panes.hpp"

#include <cstdio>
#include <string>

#include "imgui.h"

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// A label column, then the field.
void rv_editor_inspector_label(const char *label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetFontSize() * 6.0f);
    ImGui::SetNextItemWidth(-1.0f);
}

// Three numbers dragged or typed. The press takes one undo step and remembers the
// values; Escape during the drag puts them back and drops that step.
void rv_editor_inspector_vec(rv_editor_app &app, const char *label, rv_editor_vec3 rv_editor_scene_object::*field,
    float speed, const std::string &id)
{
    rv_editor_scene_doc &doc = *app.scene;
    rv_editor_scene_ui &ui = app.scene_ui;
    const int at = rv_editor_scene_find(doc.scene, id);
    if (at < 0) {
        return;
    }
    rv_editor_vec3 value = doc.scene.objects[static_cast<size_t>(at)].*field;
    rv_editor_inspector_label(label);
    const bool changed = ImGui::DragScalarN((std::string("##") + label).c_str(), ImGuiDataType_Double, value.data(), 3,
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

// A text value committed as a whole, never letter by letter.
void rv_editor_inspector_text(rv_editor_app &app, const char *label, std::string rv_editor_scene_object::*field,
    char *buf, size_t size, const std::string &id, const rv_editor_theme &theme, const char *drop)
{
    rv_editor_scene_doc &doc = *app.scene;
    const int at = rv_editor_scene_find(doc.scene, id);
    rv_editor_inspector_label(label);
    rv_editor_text_field((std::string("##") + label).c_str(), buf, size, theme);
    const bool commit = ImGui::IsItemDeactivatedAfterEdit();
    std::string dropped;
    if (drop != nullptr && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(drop)) {
            dropped = static_cast<const char *>(p->Data);
        }
        ImGui::EndDragDropTarget();
    }
    if (at < 0 || (!commit && dropped.empty())) {
        return;
    }
    const std::string value = dropped.empty() ? std::string(buf) : dropped;
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
    if (app.scene == nullptr) {
        ImGui::TextWrapped("No scene is open.");
        rv_editor_scene_open_row(app, theme);
        return;
    }
    rv_editor_scene_keys(app);
    rv_editor_scene_doc &doc = *app.scene;
    rv_editor_scene_ui &ui = app.scene_ui;
    const int at = rv_editor_scene_find(doc.scene, doc.selected);
    if (at < 0) {
        ImGui::TextWrapped("Select an object in Hierarchy or in the Scene.");
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
        ImGui::TextWrapped("Read-only: %s", doc.scene.read_only.c_str());
    }
    ImGui::BeginDisabled(read_only);
    rv_editor_inspector_text(app, "Name", &rv_editor_scene_object::name, ui.name, sizeof(ui.name), o.id, theme, nullptr);
    rv_editor_inspector_label("Kind");
    ImGui::TextUnformatted(o.kind == "mesh" ? "box (mesh)" : o.kind.c_str());
    rv_editor_inspector_label("Id");
    ImGui::TextDisabled("%s", o.id.c_str());
    ImGui::SeparatorText("Transform, in the parent's space");
    rv_editor_inspector_vec(app, "Position", &rv_editor_scene_object::position, 0.01f, o.id);
    rv_editor_inspector_vec(app, "Rotation", &rv_editor_scene_object::rotation, 0.5f, o.id);
    ImGui::SetItemTooltip("Degrees: yaw about Y, then pitch about X, then roll about Z");
    rv_editor_inspector_vec(app, "Scale", &rv_editor_scene_object::scale, 0.01f, o.id);
    if (o.kind == "mesh") {
        ImGui::SeparatorText("Resources");
        rv_editor_inspector_text(app, "Mesh", &rv_editor_scene_object::mesh, ui.mesh, sizeof(ui.mesh), o.id, theme,
            "RV_ASSET");
        ImGui::SetItemTooltip("A disc asset; empty draws a unit box. Drop one from Assets here.");
        rv_editor_inspector_text(app, "Texture", &rv_editor_scene_object::texture, ui.texture, sizeof(ui.texture), o.id,
            theme, "RV_ASSET");
        ImGui::SetItemTooltip("A disc texture. Drop one from Assets here.");
    }
    if (!o.extra.empty()) {
        ImGui::SeparatorText("Kept as read");
        for (const auto &e : o.extra) {
            ImGui::TextDisabled("%s", e.key.c_str());
        }
        ImGui::SetItemTooltip("Keys this editor does not know; they are written back unchanged");
    }
    ImGui::EndDisabled();
}

} // namespace rv_editor
