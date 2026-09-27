// Scene in the main menu: the scene document's commands (LAY-07).

#include <string>
#include <vector>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"

namespace rv_editor
{

// The scene document's commands (LAY-07); each edit is one undo step.
void rv_editor_menu_scene(rv_editor_shell &shell)
{
    rv_editor_app &app = shell.app;
    const char *no_project = app.project.open ? nullptr : "No project is open";
    if (rv_editor_menu_item("New Scene", nullptr, no_project)) {
        rv_editor_app_scene_create(app);
    }
    if (ImGui::BeginMenu("Open Scene", app.project.open)) {
        const std::vector<std::filesystem::path> files = rv_editor_app_scene_files(app);
        for (const std::filesystem::path &path : files) {
            if (ImGui::MenuItem(path.filename().string().c_str())) {
                rv_editor_app_scene_open(app, path);
            }
        }
        if (files.empty()) {
            ImGui::TextDisabled("No scenes/*.scene.toml in this project");
        }
        ImGui::EndMenu();
    }
    const char *no_scene = app.scene == nullptr ? "No scene is open" : nullptr;
    const char *why_not_save = no_scene != nullptr ? no_scene
        : !app.scene->scene.read_only.empty()      ? "The scene is read-only"
        : !app.scene->dirty                        ? "Nothing to save"
                                                   : nullptr;
    if (rv_editor_menu_item("Save Scene", nullptr, why_not_save)) {
        std::string error;
        if (!rv_editor_app_scene_save(app, error)) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "scene not saved: " + error);
        }
    }
    ImGui::Separator();
    if (rv_editor_menu_item("Undo Scene Edit", nullptr,
            no_scene != nullptr ? no_scene : app.scene->undo.empty() ? "Nothing to undo" : nullptr)) {
        rv_editor_scene_undo(*app.scene);
    }
    if (rv_editor_menu_item("Redo Scene Edit", nullptr,
            no_scene != nullptr ? no_scene : app.scene->redo.empty() ? "Nothing to redo" : nullptr)) {
        rv_editor_scene_redo(*app.scene);
    }
    ImGui::Separator();
    // New objects go under the selected group, else at the root.
    const char *read_only = no_scene != nullptr ? no_scene
        : !app.scene->scene.read_only.empty()   ? "The scene is read-only"
                                                : nullptr;
    if (ImGui::BeginMenu("Add", read_only == nullptr)) {
        const int sel = rv_editor_scene_find(app.scene->scene, app.scene->selected);
        const std::string parent = sel >= 0 && app.scene->scene.objects[static_cast<size_t>(sel)].kind == "group"
            ? app.scene->selected
            : std::string();
        for (const char *kind : { "group", "camera", "mesh" }) {
            if (ImGui::MenuItem(std::string(kind) == "mesh" ? "Box" : std::string(kind) == "camera" ? "Camera" : "Group")) {
                rv_editor_scene_add(*app.scene, kind, parent);
            }
        }
        ImGui::EndMenu();
    }
    const char *no_selection = read_only != nullptr ? read_only
        : app.scene->selected.empty()             ? "Nothing is selected"
                                                  : nullptr;
    if (rv_editor_menu_item("Duplicate", nullptr, no_selection)) {
        rv_editor_scene_duplicate(*app.scene, app.scene->selected);
    }
    if (rv_editor_menu_item("Delete", nullptr, no_selection)) {
        rv_editor_scene_delete(*app.scene, app.scene->selected);
    }
    if (rv_editor_menu_item("Move to Root", nullptr, no_selection)) {
        std::string why;
        if (!rv_editor_scene_reparent(*app.scene, app.scene->selected, "", true, why)) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "not moved: " + why);
        }
    }
}

} // namespace rv_editor
