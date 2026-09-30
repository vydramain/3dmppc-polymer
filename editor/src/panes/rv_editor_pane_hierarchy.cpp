// Hierarchy: the scene's objects as a tree. Selection is by id and shared with the
// viewport and Inspector; dragging one onto another moves it there keeping where
// it is in the scene, or says why it cannot (SCN-02, SCN-03).

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

constexpr const char *rv_editor_scene_payload = "RV_SCENE_OBJECT";

const char *rv_editor_scene_kind_label(const std::string &kind)
{
    return kind == "mesh" ? "box" : kind.c_str();
}

void rv_editor_hierarchy_move(rv_editor_app &app, const std::string &id, const std::string &parent, bool keep_world)
{
    std::string why;
    app.scene_ui.note = rv_editor_scene_reparent(*app.scene, id, parent, keep_world, why) ? std::string()
                                                                                           : "Not moved: " + why;
}

void rv_editor_hierarchy_node(rv_editor_app &app, size_t index, bool read_only)
{
    rv_editor_scene_doc &doc = *app.scene;
    const rv_editor_scene_object o = doc.scene.objects[index];
    std::vector<size_t> children;
    for (size_t i = 0; i < doc.scene.objects.size(); ++i) {
        if (doc.scene.objects[i].parent == o.id) {
            children.push_back(i);
        }
    }
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_DefaultOpen;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (doc.selected == o.id) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const std::string label = (o.name.empty() ? std::string("(unnamed)") : o.name) + "  [" +
        rv_editor_scene_kind_label(o.kind) + "]";
    const bool open = ImGui::TreeNodeEx(o.id.c_str(), flags, "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        doc.selected = o.id;
    }
    if (!read_only && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(rv_editor_scene_payload, o.id.c_str(), o.id.size() + 1);
        ImGui::Text("Move %s", label.c_str());
        ImGui::EndDragDropSource();
    }
    if (!read_only && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(rv_editor_scene_payload)) {
            rv_editor_hierarchy_move(app, static_cast<const char *>(p->Data), o.id, true);
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem()) {
        doc.selected = o.id;
        ImGui::BeginDisabled(read_only);
        for (const char *kind : { "group", "camera", "mesh", "quad", "billboard", "volume" }) {
            if (ImGui::MenuItem(("Add " + std::string(rv_editor_scene_kind_label(kind)) + " Under It").c_str())) {
                rv_editor_scene_add(doc, kind, o.id);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
            rv_editor_scene_duplicate(doc, o.id);
        }
        if (ImGui::MenuItem("Delete", "Delete")) {
            rv_editor_scene_delete(doc, o.id);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Move to Root")) {
            rv_editor_hierarchy_move(app, o.id, "", true);
        }
        // The other meaning, asked for by name: the numbers stay, the place changes.
        if (ImGui::MenuItem("Move to Root (Keep Local Values)")) {
            rv_editor_hierarchy_move(app, o.id, "", false);
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (open) {
        for (const size_t child : children) {
            if (child < app.scene->scene.objects.size()) {
                rv_editor_hierarchy_node(app, child, read_only);
            }
        }
        ImGui::TreePop();
    }
}

} // namespace

void rv_editor_scene_keys(rv_editor_app &app)
{
    if (app.scene == nullptr || !ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) ||
        ImGui::GetIO().WantTextInput) {
        return;
    }
    rv_editor_scene_doc &doc = *app.scene;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
        rv_editor_scene_undo(doc);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) {
        rv_editor_scene_redo(doc);
    }
    if (!doc.scene.read_only.empty() || doc.selected.empty()) {
        return;
    }
    if (ImGui::IsKeyChordPressed(ImGuiKey_Delete)) {
        rv_editor_scene_delete(doc, doc.selected);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
        rv_editor_scene_duplicate(doc, doc.selected);
    }
}

void rv_editor_pane_hierarchy(rv_editor_app &app, const rv_editor_theme &theme)
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
    const bool read_only = !doc.scene.read_only.empty();
    rv_editor_shelf_begin("##shelf", theme);
    ImGui::BeginDisabled(read_only);
    for (const char *kind : { "group", "camera", "mesh", "quad", "billboard", "volume" }) {
        const std::string label = std::string("+ ") + rv_editor_scene_kind_label(kind);
        rv_editor_flow(rv_editor_button_width(label.c_str()));
        if (rv_editor_button(label.c_str(), theme)) {
            // Under the selected group, else at the root.
            const int sel = rv_editor_scene_find(doc.scene, doc.selected);
            const bool group = sel >= 0 && doc.scene.objects[static_cast<size_t>(sel)].kind == "group";
            rv_editor_scene_add(doc, kind, group ? doc.selected : std::string());
        }
    }
    ImGui::EndDisabled();
    rv_editor_shelf_end();

    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const auto body = [&]() {
        if (!app.scene_ui.note.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.warning));
            ImGui::TextWrapped("%s", app.scene_ui.note.c_str());
            ImGui::PopStyleColor();
        }
        for (size_t i = 0; i < doc.scene.objects.size(); ++i) {
            // Roots, and objects whose parent is missing, which the tree would otherwise hide.
            const std::string &parent = doc.scene.objects[i].parent;
            if (parent.empty() || rv_editor_scene_find(doc.scene, parent) < 0) {
                rv_editor_hierarchy_node(app, i, read_only);
            }
            if (app.scene == nullptr) {
                return;
            }
        }
        // The rest of the pane is the root: dropped here, an object leaves its parent.
        const ImVec2 rest = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(rest.x, std::max(rest.y, ImGui::GetFrameHeight())));
        if (!read_only && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(rv_editor_scene_payload)) {
                rv_editor_hierarchy_move(app, static_cast<const char *>(p->Data), "", true);
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemClicked()) {
            doc.selected.clear();
        }
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
