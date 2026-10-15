#pragma once

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

} // namespace rv_editor
