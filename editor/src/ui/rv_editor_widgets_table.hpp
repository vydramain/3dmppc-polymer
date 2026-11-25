#pragma once

#include <string>

#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

// Tables: ImGui's BeginTable in the theme's colours (the table colours come from the
// theme's ImGui style). A table does not scroll; its well does (rv_editor_well_begin).

// With stretch_prop, stretched columns share the width left over; fixed columns take their widest cell or header.
enum class rv_editor_table_width {
    stretch,
    fixed,
};

struct rv_editor_table_column {
    const char *name = ""; // header text; "" for a column with none
    rv_editor_table_width width = rv_editor_table_width::stretch;
    float fixed_px = 0.0f; // a fixed column's width in px, whatever its cells hold; 0 = its widest cell or header
    // A stretch column's share of the room; 0 sizes it by its first cells, which moves it with the content.
    float weight = 0.0f;
    // A pinned column stays at that edge of the visible area while the table scrolls sideways (status, keys).
    bool pinned_left = false;
    bool pinned_right = false;
};

// How the table takes the width: stretch_prop shares the window's width among stretched
// columns. fixed_fit makes every column WidthFixed, whatever its description says: each
// takes its widest cell or header.
enum class rv_editor_table_sizing {
    stretch_prop,
    fixed_fit,
};

struct rv_editor_table_style {
    bool borders = false;   // grid lines around and between cells
    bool row_bands = false; // alternate row fill
    bool header = false;    // column names above the rows, scrolling with them
    rv_editor_table_sizing sizing = rv_editor_table_sizing::stretch_prop;
    bool fill_last = false; // fixed_fit only: the last column takes the width left over, so the table's rows end at its edge
};

// Starts a table of `count` columns and draws its header when the style asks for one.
// Rows and cells go between begin and end; end only when begin returned true.
bool rv_editor_table_begin(const char *id,
    const rv_editor_table_column *columns,
    int count,
    const rv_editor_table_style &style = {});
void rv_editor_table_end();

// Starts a row; the next cell is its first column. A cell returns false when its
// column is clipped and the caller may skip drawing it.
void rv_editor_table_row();
bool rv_editor_table_cell();

// `text` cut to fit `width` px on one line, ending in "..." when it was cut; whole when it fits.
std::string rv_editor_ellipsize(const char *text, float width);

// Cell text on one line: ellipsized to the column's remaining width, the full text in the item's tooltip.
void rv_editor_table_cell_text(const char *text,
    const rv_editor_theme &theme,
    rv_editor_label_kind kind = rv_editor_label_kind::normal);

// What a selectable table row did: a single click anywhere on it selects; a double click or Enter
// on the focused row is the row's main action.
struct rv_editor_table_row_result {
    bool clicked = false;
    bool activated = false;
};

// The row's click area across every column, in the first cell, after rv_editor_table_row. `selected` fills it.
// `id` is also the Selectable's label, so it must be a hidden one ("##row0"); draw the cells' content after it.
rv_editor_table_row_result rv_editor_table_row_select(const char *id, bool selected, const rv_editor_theme &theme);

} // namespace rv_editor
