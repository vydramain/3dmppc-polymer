// Toolchest: Scene's tools for the scene document in front; in Debug, the
// session's: Mark Moment, Capture Frame, Report Issue and Restart.

#include "panes/rv_editor_panes.hpp"

#include <string>

#include "imgui.h"

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_pane_mark_tools.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

namespace
{

void rv_editor_session_tools(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    (void)rv_editor_mark_tools(app, theme, ui, true);
    // On the second row, a reserved line: the newest mark, or that there is none yet.
    const std::string last_mark = app.marks.empty() ?
        std::string(rv_editor_text("pane_toolchest.no_mark")) :
        rv_editor_text_format("pane_toolchest.last_mark", std::make_format_args(app.marks.back()));
    rv_editor_message_line("##last_mark", last_mark.c_str(), 1, 0.0f, theme);
}

} // namespace

void rv_editor_pane_toolchest(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    // Only controls, no separate content: the whole pane is a shelf.
    rv_editor_shelf_begin("##shelf", theme);
    // The tallest state's height (the scene tools' two rows) is reserved for every state, so the strip never changes.
    const float height = 2.0f * ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("##tools", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (app.preset == rv_editor_layout_preset::debug) {
        rv_editor_session_tools(app, theme, ui);
    } else if (app.scene != nullptr) {
        // Scene's tools act on a scene document; without one, none is offered.
        rv_editor_scene_tools(theme, ui);
    } else {
        rv_editor_message_line("##no_scene", rv_editor_text("pane_toolchest.no_scene"), 2, 0.0f, theme);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    rv_editor_shelf_end();
}

} // namespace rv_editor
