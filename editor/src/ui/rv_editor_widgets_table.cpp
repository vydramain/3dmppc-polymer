// Tables: BeginTable with the editor's column widths, sizing and style choices.
// The table colours come from the theme's ImGui style.

#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

bool rv_editor_table_begin(const char *id, const rv_editor_table_column *columns, int count, const rv_editor_table_style &style)
{
    const ImGuiTableFlags sizing =
        style.sizing == rv_editor_table_sizing::fixed_fit ? ImGuiTableFlags_SizingFixedFit : ImGuiTableFlags_SizingStretchProp;
    const ImGuiTableFlags borders = style.borders ? ImGuiTableFlags_Borders : ImGuiTableFlags_None;
    const ImGuiTableFlags bands = style.row_bands ? ImGuiTableFlags_RowBg : ImGuiTableFlags_None;
    if (!ImGui::BeginTable(id, count, sizing | borders | bands)) {
        return false;
    }
    const bool fit = style.sizing == rv_editor_table_sizing::fixed_fit;
    for (int i = 0; i < count; ++i) {
        const bool fixed = fit || columns[i].width == rv_editor_table_width::fixed;
        const ImGuiTableColumnFlags width = fixed ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch;
        ImGui::TableSetupColumn(columns[i].name, width);
    }
    if (style.header) {
        ImGui::TableHeadersRow();
    }
    return true;
}

void rv_editor_table_end()
{
    ImGui::EndTable();
}

void rv_editor_table_row()
{
    ImGui::TableNextRow();
}

bool rv_editor_table_cell()
{
    return ImGui::TableNextColumn();
}

} // namespace rv_editor
