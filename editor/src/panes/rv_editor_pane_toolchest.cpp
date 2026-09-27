// Toolchest: Scene's tools for the scene document in front.

#include "panes/rv_editor_panes.hpp"

#include "imgui.h"

namespace rv_editor
{

void rv_editor_pane_toolchest(rv_editor_app &app, const rv_editor_theme &theme)
{
    // Scene's tools act on a scene document; without one, none is offered.
    if (app.scene != nullptr) {
        rv_editor_scene_tools(app, theme);
        return;
    }
    ImGui::TextWrapped("Select, Move, Rotate and Scale act on a scene document: Scene > New Scene or Open Scene.");
}

} // namespace rv_editor
