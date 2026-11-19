// Tables: BeginTable with the editor's column widths, sizing and style choices.
// The table colours come from the theme's ImGui style.

#include <algorithm>

#include "imgui_internal.h" // the table's first-frame flag
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_table.hpp"

namespace rv_editor
{

namespace
{

// shortcut: one table at a time and at most 16 columns, as the editor has no nested tables; lift when one appears.
constexpr int pinned_columns_max = 16;

struct rv_editor_pinned_state {
    bool left[pinned_columns_max] = {};
    bool right[pinned_columns_max] = {};
    int count = 0;
    bool active = false; // the current cell is shifted to the visible edge
    float shift = 0.0f;  // how far from where the column lays it
} rv_editor_pinned;

// Puts the cell the table just started at its edge of the visible area, over what scrolls beneath it. True for a
// pinned column, whose cell is then to be drawn even where ImGui clipped its column away.
bool rv_editor_pinned_begin()
{
    ImGuiTable *table = ImGui::GetCurrentTable();
    const int n = table->CurrentColumn;
    if (n >= rv_editor_pinned.count || !(rv_editor_pinned.left[n] || rv_editor_pinned.right[n])) {
        return false;
    }
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    const ImGuiTableColumn &column = table->Columns[n];
    const float natural = window->DC.CursorPos.x;
    const float width = column.WorkMaxX - column.WorkMinX;
    const float target = rv_editor_pinned.right[n] ? std::min(natural, window->InnerRect.Max.x - width) :
                                                     std::max(natural, window->InnerRect.Min.x + table->CellPaddingX);
    rv_editor_pinned.shift = target - natural;
    if (rv_editor_pinned.shift == 0.0f) {
        return true;
    }
    rv_editor_pinned.active = true;
    window->SkipItems = false; // a column scrolled out of view is skipped, its pinned cell is not
    // Over the other cells' text, which scrolls beneath: in the channel of the last column that is drawn.
    for (int i = table->ColumnsCount - 1; i >= 0; --i) {
        const ImGuiTableColumn &c = table->Columns[i];
        if (c.IsRequestOutput && c.DrawChannelCurrent < table->DrawSplitter->_Count) {
            table->DrawSplitter->SetCurrentChannel(window->DrawList, c.DrawChannelCurrent);
            break;
        }
    }
    ImGui::PushClipRect(window->InnerRect.Min, window->InnerRect.Max, false);
    const ImVec2 min(target - table->CellPaddingX, table->RowPosY1);
    window->DrawList->AddRectFilled(min,
        ImVec2(target + width + table->CellPaddingX, table->RowPosY1 + ImGui::GetFrameHeight()),
        ImGui::GetColorU32(ImGuiCol_ChildBg)); // the content area's code background
    window->DC.CursorPos.x = target;
    window->DC.CursorMaxPos.x = target;
    window->DC.CursorPosPrevLine.x = target;
    window->DC.ColumnsOffset.x += rv_editor_pinned.shift;
    return true;
}

// Gives the column its own place back: the table sizes it by where the cell's content ended.
void rv_editor_pinned_end()
{
    if (!rv_editor_pinned.active) {
        return;
    }
    rv_editor_pinned.active = false;
    ImGui::GetCurrentWindow()->DC.CursorMaxPos.x -= rv_editor_pinned.shift;
    ImGui::PopClipRect();
}

} // namespace

bool rv_editor_table_begin(const char *id, const rv_editor_table_column *columns, int count, const rv_editor_table_style &style)
{
    const ImGuiTableFlags sizing =
        style.sizing == rv_editor_table_sizing::fixed_fit ? ImGuiTableFlags_SizingFixedFit : ImGuiTableFlags_SizingStretchProp;
    // Fixed columns take their own width, so a table of them is as wide as they are, past its area when they are wider.
    const ImGuiTableFlags fit_x = style.sizing == rv_editor_table_sizing::fixed_fit ?
        ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_NoKeepColumnsVisible :
        ImGuiTableFlags_None;
    const ImGuiTableFlags borders = style.borders ? ImGuiTableFlags_Borders : ImGuiTableFlags_None;
    const ImGuiTableFlags bands = style.row_bands ? ImGuiTableFlags_RowBg : ImGuiTableFlags_None;
    if (!ImGui::BeginTable(id, count, sizing | fit_x | borders | bands)) {
        return false;
    }
    const bool fit = style.sizing == rv_editor_table_sizing::fixed_fit;
    for (int i = 0; i < count; ++i) {
        const bool fixed = fit || columns[i].width == rv_editor_table_width::fixed;
        const ImGuiTableColumnFlags width = fixed ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch;
        ImGui::TableSetupColumn(columns[i].name, width, fixed ? columns[i].fixed_px : columns[i].weight);
    }
    if (style.header) {
        ImGui::TableHeadersRow();
    }
    rv_editor_pinned.count = std::min(count, pinned_columns_max);
    for (int i = 0; i < rv_editor_pinned.count; ++i) {
        rv_editor_pinned.left[i] = columns[i].pinned_left;
        rv_editor_pinned.right[i] = columns[i].pinned_right;
    }
    return true;
}

void rv_editor_table_end()
{
    rv_editor_pinned_end();
    ImGui::EndTable();
}

void rv_editor_table_row()
{
    rv_editor_pinned_end();
    ImGui::TableNextRow();
}

bool rv_editor_table_cell()
{
    rv_editor_pinned_end();
    const bool shown = ImGui::TableNextColumn();
    return rv_editor_pinned_begin() || shown;
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
    rv_editor_label(text, kind, theme, rv_editor_label_layout::line); // whole: the content area scrolls
}

rv_editor_table_row_result rv_editor_table_row_select(const char *id, bool selected, const rv_editor_theme &theme)
{
    // The row spans the whole table, not just the columns ImGui kept visible: widen its parent rect to the last column.
    ImGuiTable *table = ImGui::GetCurrentTable();
    const ImRect parent = table->InnerWindow->ParentWorkRect;
    table->InnerWindow->ParentWorkRect.Max.x =
        std::max(parent.Max.x, table->Columns[table->RightMostEnabledColumn].MaxX - table->OuterPaddingX);
    rv_editor_list_style style;
    style.selected = selected;
    style.span_columns = true;
    style.height = ImGui::GetFrameHeight(); // as tall as a button of the row, so a click beside one selects too
    style.overlap = true;                   // the buttons of the row's cells stay clickable
    const rv_editor_list_result list = rv_editor_list_row(id, id, theme, style);
    table->InnerWindow->ParentWorkRect = parent;
    rv_editor_table_row_result result;
    result.clicked = list.clicked;
    // No key repeat: a held Enter acts once. A double click is the mouse's second press.
    result.activated = list.double_clicked || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false));
    return result;
}

} // namespace rv_editor
