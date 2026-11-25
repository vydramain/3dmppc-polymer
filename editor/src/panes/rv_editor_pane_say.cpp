#include "panes/rv_editor_pane_say.hpp"

#include "imgui_internal.h" // an item added without taking room

#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

rv_editor_message_say rv_editor_say_no_project()
{
    return { rv_editor_text("panes.no_project_open_message"),
        rv_editor_label_kind::normal,
        { rv_editor_text("panes.open_project_box") } };
}

void rv_editor_say_whole_text()
{
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::ItemAdd(rect, ImHashData(&rect.Min, sizeof(rect.Min), ImGui::GetID("##whole")), nullptr, ImGuiItemFlags_NoNav);
}

float rv_editor_say_shelf_width(std::initializer_list<const char *> labels)
{
    float row = 0.0f;
    for (const char *label : labels) {
        row += rv_editor_button_width(label);
    }
    return rv_editor_tiled_shelf_width(row + static_cast<float>(labels.size() - 1) * ImGui::GetStyle().ItemSpacing.x);
}

} // namespace rv_editor
