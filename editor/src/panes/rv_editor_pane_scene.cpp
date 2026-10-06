// Scene: the open scene document; without one, what a scene is and how to get one.
// A game draws a scene only through a loader.

#include "panes/rv_editor_panes.hpp"

#include <string>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

void rv_editor_scene_open_row(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (rv_editor_button(rv_editor_text("pane_scene.create_scene"), theme)) {
        rv_editor_shell_new_scene_request(app);
    }
    ImGui::SetItemTooltip("%s", rv_editor_text("pane_scene.create_scene_tooltip"));
    for (const std::filesystem::path &path : rv_editor_app_scene_files(app)) {
        ImGui::SameLine();
        const std::string filename = path.filename().string();
        const auto open_label = rv_editor_text_format("pane_scene.open_file",
            std::make_format_args(filename));
        if (rv_editor_button(open_label.c_str(), theme)) {
            rv_editor_app_scene_open(app, path);
        }
    }
}

void rv_editor_pane_scene(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    rv_editor_scene_new_area(app, theme);
    if (!rv_editor_scene_tabs_draw(app, renderer, theme)) {
        return; // a picture or a sound tab is in front; it drew its own content
    }
    if (app.scene == nullptr) {
        ImGui::TextWrapped("%s", rv_editor_text("pane_scene.no_scene_open"));
        if (!app.scene_error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
            ImGui::TextWrapped("%s", app.scene_error.c_str());
            ImGui::PopStyleColor();
        }
        rv_editor_scene_open_row(app, theme);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", rv_editor_text("pane_scene.game_loader_explanation"));
        ImGui::PopStyleColor();
        return;
    }
    if (!app.scene->scene.read_only.empty()) {
        const auto read_only_msg = rv_editor_text_format("pane_scene.read_only_format",
            std::make_format_args(app.scene->scene.read_only));
        ImGui::TextWrapped("%s", read_only_msg.c_str());
    }
    if (!app.scene_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.error));
        ImGui::TextWrapped("%s", app.scene_error.c_str());
        ImGui::PopStyleColor();
    }
    rv_editor_scene_viewport(app, renderer, theme);
}

} // namespace rv_editor
