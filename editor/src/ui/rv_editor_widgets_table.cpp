// Tables: BeginTable with the editor's column widths, sizing and style choices.
// The table colours come from the theme's ImGui style.

#include "imgui_internal.h" // the table's first-frame flag
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

std::string rv_editor_ellipsize(const char *text, float width)
{
    const std::string whole = text;
    if (ImGui::CalcTextSize(text).x <= width) {
        return whole;
    }
    // Longest prefix, cut at a character boundary, that fits with the dots; a binary search on its byte length.
    const auto fits = [&](size_t n) {
        return ImGui::CalcTextSize((whole.substr(0, n) + "...").c_str()).x <= width;
    };
    size_t lo = 0;
    size_t hi = whole.size();
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        size_t cut = mid;
        while (cut > 0 && (static_cast<unsigned char>(whole[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        if (fits(cut)) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    size_t cut = lo;
    while (cut > 0 && (static_cast<unsigned char>(whole[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return fits(cut) ? whole.substr(0, cut) + "..." : std::string();
}

void rv_editor_table_cell_text(const char *text, const rv_editor_theme &theme, rv_editor_label_kind kind)
{
    // A table's first frame has not sized its columns yet: lay the text out against the window's room then.
    const ImGuiTable *table = ImGui::GetCurrentTable();
    const float room = table != nullptr && table->IsInitializing ?
        ImGui::GetCurrentWindow()->ContentRegionRect.Max.x - ImGui::GetCursorScreenPos().x :
        ImGui::GetContentRegionAvail().x;
    const std::string shown = rv_editor_ellipsize(text, room);
    rv_editor_label(shown.c_str(), kind, theme, rv_editor_label_layout::line);
    if (shown != text && ImGui::IsItemHovered()) {
        rv_editor_tooltip(text);
    }
}

} // namespace rv_editor
