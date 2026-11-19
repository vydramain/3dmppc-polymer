// What Scene, Hierarchy and Inspector share when there is no scene to work on: the message box that says so with its
// button, and the area that lists the project's scenes to open.

#include "panes/rv_editor_panes.hpp"

#include <string>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

float rv_editor_scene_none_width()
{
    return rv_editor_message_box_min_width(
        { { rv_editor_text("panes.open_project_box") }, { rv_editor_text("pane_scene.create_scene") } });
}

void rv_editor_scene_none_box(rv_editor_app &app,
    const std::string &text,
    rv_editor_label_kind kind,
    const rv_editor_theme &theme,
    float width)
{
    if (!app.project.open) {
        if (rv_editor_message_box("##note",
                rv_editor_text("panes.no_project_open_message"),
                rv_editor_label_kind::normal,
                { rv_editor_text("panes.open_project_box") },
                theme,
                width) == 0) {
            app.open_folder_request = true;
        }
        return;
    }
    if (rv_editor_message_box("##note", text, kind, { rv_editor_text("pane_scene.create_scene") }, theme, width) == 0) {
        rv_editor_shell_new_scene_request(app);
    }
}

void rv_editor_scene_files_list(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        return;
    }
    const std::vector<std::filesystem::path> files = rv_editor_app_scene_files(app);
    if (files.empty()) {
        rv_editor_label(rv_editor_text("shell_menu_scene.no_scenes"), rv_editor_label_kind::dim, theme);
    }
    for (const std::filesystem::path &path : files) {
        const std::string name = path.filename().string();
        if (rv_editor_list_row(name.c_str(), name.c_str(), theme).clicked) {
            rv_editor_app_scene_open(app, path);
        }
    }
}

} // namespace rv_editor
