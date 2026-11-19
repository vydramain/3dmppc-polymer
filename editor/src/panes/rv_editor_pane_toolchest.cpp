// Toolchest: Scene's tools for the scene document in front; in Debug, the
// session's: Mark Moment, Capture Frame, Report Issue and Restart.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "imgui.h"
#include "imgui_internal.h" // an item added without taking room

#include "app/rv_editor_shell.hpp"
#include "panes/rv_editor_pane_mark_tools.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_text.hpp"
#include "ui/rv_editor_widgets_tile.hpp"

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

// The shelf's rows: the tools, then the second row (the newest mark, the Snap row, or the second line of "no scene").
float rv_editor_toolchest_rows()
{
    return 2.0f * ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
}

} // namespace

rv_editor_size rv_editor_toolchest_minimum()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    // The widest row, as Runtime Controls': the window's and the shelf's left padding, the row to the tile's edge.
    const float row = std::max(rv_editor_mark_tools_min_width(rv_editor_theme_olive, true), rv_editor_scene_tools_min_width());
    return { static_cast<int32_t>(std::ceil(style.WindowPadding.x + style.ItemInnerSpacing.x + row)),
        static_cast<int32_t>(std::ceil(
            style.WindowPadding.y + 2.0f * style.ItemInnerSpacing.y + rv_editor_toolchest_rows() + style.ItemSpacing.y)) };
}

void rv_editor_pane_toolchest(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui)
{
    // Only controls, no separate content: the whole pane is a shelf.
    rv_editor_shelf_begin("##shelf", theme);
    // The tallest state's height (the scene tools' two rows) is reserved for every state, so the strip never changes.
    const float height = rv_editor_toolchest_rows();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("##tools", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (app.preset == rv_editor_layout_preset::debug) {
        rv_editor_session_tools(app, theme, ui);
    } else if (app.scene != nullptr) {
        // Scene's tools act on a scene document; without one, none is offered.
        rv_editor_scene_tools(theme, ui);
    } else {
        rv_editor_message_line("##no_scene", rv_editor_text("pane_toolchest.no_scene"), 3, 0.0f, theme);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    rv_editor_shelf_end();
    // The spacing the tile's content ends with is an item too, so a tile one pixel lower than its minimum is seen to cut it.
    const ImVec2 end = ImGui::GetCursorScreenPos();
    ImGui::ItemAdd(ImRect(ImVec2(end.x, end.y - ImGui::GetStyle().ItemSpacing.y), end),
        ImGui::GetID("##end"),
        nullptr,
        ImGuiItemFlags_NoNav);
}

} // namespace rv_editor
