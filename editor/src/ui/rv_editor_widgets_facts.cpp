// The facts list: a content area holding a key and value table.

#include "ui/rv_editor_widgets_facts.hpp"

#include <algorithm>

#include "imgui_internal.h" // the window's inner rect, which excludes its scrollbars
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_table.hpp"

namespace rv_editor
{

namespace
{

int rv_editor_facts_rows = 0; // ids the rows apart even when two keys are equal

} // namespace

void rv_editor_facts_begin(const char *id, ImVec2 size, const rv_editor_theme &theme)
{
    rv_editor_content_begin(id, size, theme);
    rv_editor_facts_rows = 0;
    const rv_editor_table_column columns[] = {
        { "", rv_editor_table_width::fixed, 0.0f, 0.0f, true },
        { "", rv_editor_table_width::fixed },
    };
    rv_editor_table_begin("##facts", columns, 2, { false, false, false, rv_editor_table_sizing::fixed_fit });
}

rv_editor_facts_row_result rv_editor_facts_row(const char *key, const char *value, const rv_editor_theme &theme)
{
    const char *shown = value != nullptr && value[0] != '\0' ? value : "-";
    rv_editor_facts_row_result result;
    ImGui::PushID(rv_editor_facts_rows++);
    rv_editor_table_row();
    rv_editor_table_cell();
    ImGui::PushStyleColor(ImGuiCol_Text, rv_editor_col(theme.code_subtext)); // keys recede behind the values
    rv_editor_table_cell_text(key, theme);
    ImGui::PopStyleColor();
    result.key_min = ImGui::GetItemRectMin();
    result.key_max = ImGui::GetItemRectMax();
    rv_editor_table_cell();
    rv_editor_table_cell_text(shown, theme);
    result.value_min = ImGui::GetItemRectMin();
    result.value_max = ImGui::GetItemRectMax();
    // The whole row, the free space beside a short value included, opens the menu; ImGui gives no item for that space.
    const ImVec2 row_min(result.key_min.x, std::min(result.key_min.y, result.value_min.y));
    const ImVec2 row_max(ImGui::GetCurrentWindow()->InnerRect.Max.x, std::max(result.key_max.y, result.value_max.y));
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::IsMouseHoveringRect(row_min, row_max, false)) {
        ImGui::OpenPopup("##facts_menu");
    }
    if (rv_editor_context_menu_begin("##facts_menu")) {
        if (rv_editor_menu_item(rv_editor_text("widgets_fields.button_copy"), nullptr, nullptr)) {
            ImGui::SetClipboardText(shown);
        }
        rv_editor_context_menu_end();
    }
    ImGui::PopID();
    return result;
}

void rv_editor_facts_end(const rv_editor_theme &theme)
{
    rv_editor_table_end();
    rv_editor_content_end(theme);
}

} // namespace rv_editor
