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

namespace
{

// The tabs work on a bare disc; its scene needs pdkkit, so the tile says why instead.
constexpr const char *no_pdkkit_text_id = "shell_pane.no_pdkkit";

} // namespace

void rv_editor_scene_open_row(rv_editor_app &app, const rv_editor_theme &theme)
{
    const float row_width = ImGui::GetContentRegionAvail().x;
    if (rv_editor_button(rv_editor_text("pane_scene.create_scene"), theme)) {
        rv_editor_shell_new_scene_request(app);
    }
    rv_editor_item_tooltip(rv_editor_text("pane_scene.create_scene_tooltip"));
    for (const std::filesystem::path &path : rv_editor_app_scene_files(app)) {
        // A file name that does not fit a row of its own keeps its end (the full name is in the tooltip).
        const std::string filename = path.filename().string();
        const std::string full = rv_editor_text_format("pane_scene.open_file", std::make_format_args(filename));
        const float chrome = rv_editor_button_width(full.c_str()) - ImGui::CalcTextSize(full.c_str()).x;
        const float room = row_width - chrome - ImGui::CalcTextSize(full.c_str()).x + ImGui::CalcTextSize(filename.c_str()).x;
        const std::string shown = rv_editor_path_tail(filename, room);
        const std::string open_label =
            rv_editor_text_format("pane_scene.open_file", std::make_format_args(shown)) + "##" + filename;
        rv_editor_flow(rv_editor_button_width(open_label.substr(0, open_label.find("##")).c_str()));
        if (rv_editor_button(open_label.c_str(), theme)) {
            rv_editor_app_scene_open(app, path);
        }
        if (shown != filename) {
            rv_editor_item_tooltip(filename.c_str());
        }
    }
}

void rv_editor_pane_scene(rv_editor_app &app, SDL_Renderer *renderer, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    const bool bare = app.project.sdk == rv_editor_sdk_state::bare;
    if (!bare) {
        rv_editor_scene_new_area(app, theme, ui);
    }
    if (!rv_editor_scene_tabs_draw(app, renderer, theme, ui)) {
        return; // a picture, sound or model tab is in front; it drew its own content
    }
    if (bare) {
        rv_editor_label(rv_editor_text(no_pdkkit_text_id), rv_editor_label_kind::dim, theme, rv_editor_label_layout::wrap);
        return;
    }
    if (app.scene == nullptr) {
        rv_editor_label(rv_editor_text("pane_scene.no_scene_open"),
            rv_editor_label_kind::normal,
            theme,
            rv_editor_label_layout::wrap);
        rv_editor_message_line("##error", app.scene_error.c_str(), 2, 0.0f, theme, nullptr, rv_editor_label_kind::error);
        rv_editor_scene_open_row(app, theme);
        rv_editor_label(rv_editor_text("pane_scene.game_loader_explanation"),
            rv_editor_label_kind::dim,
            theme,
            rv_editor_label_layout::wrap);
        return;
    }
    // The read-only line and the error keep their places, empty when there is nothing to say, so the viewport stays.
    std::string read_only_msg;
    if (!app.scene->scene.read_only.empty()) {
        read_only_msg = rv_editor_text_format("pane_scene.read_only_format", std::make_format_args(app.scene->scene.read_only));
    }
    rv_editor_message_line("##read_only", read_only_msg.c_str(), 1, 0.0f, theme);
    rv_editor_message_line("##error", app.scene_error.c_str(), 2, 0.0f, theme, nullptr, rv_editor_label_kind::error);
    rv_editor_scene_viewport(app, renderer, theme, ui);
}

} // namespace rv_editor
