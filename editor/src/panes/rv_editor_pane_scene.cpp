// Scene: the open scene document; without one, what a scene is and how to get one
// (TPL-04). A game draws a scene only through a loader.

#include "panes/rv_editor_panes.hpp"

#include <string>

#include "imgui.h"

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

void rv_editor_scene_open_row(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (rv_editor_button("Create Scene", theme)) {
        rv_editor_app_scene_create(app);
    }
    ImGui::SetItemTooltip("A camera and a box in scenes/, under a name no file has");
    for (const std::filesystem::path &path : rv_editor_app_scene_files(app)) {
        ImGui::SameLine();
        if (rv_editor_button(("Open " + path.filename().string()).c_str(), theme)) {
            rv_editor_app_scene_open(app, path);
        }
    }
}

void rv_editor_pane_scene(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    if (app.scene == nullptr) {
        ImGui::TextWrapped("No scene is open. A scene is a file in scenes/ with objects in it: groups, cameras "
                           "and boxes, each with a position, rotation and scale.");
        if (!app.scene_error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
            ImGui::TextWrapped("%s", app.scene_error.c_str());
            ImGui::PopStyleColor();
        }
        rv_editor_scene_open_row(app, theme);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("The game shows a scene only if its code reads it through a loader, "
                           "pdklib/rv_scene, as example-cpp does. Geometry written in C++ is not a scene "
                           "here, and nothing is converted into one.");
        ImGui::PopStyleColor();
        return;
    }
    // The viewport draws here; until then, what the document holds.
    const rv_editor_scene &scene = app.scene->scene;
    ImGui::Text("%s: %zu objects%s", rv_editor_app_scene_name(app).c_str(), scene.objects.size(),
        app.scene->dirty ? ", unsaved" : "");
    if (!scene.read_only.empty()) {
        ImGui::TextWrapped("Read-only: %s", scene.read_only.c_str());
    }
}

} // namespace rv_editor
