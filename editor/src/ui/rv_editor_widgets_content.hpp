#pragma once

// The content area of a tile: a Catppuccin well for lists, trees, tables and records. It scrolls along both axes only
// when its content overflows, shows every row whole and never ends a text in "...".

#include "imgui.h"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets_status.hpp"

namespace rv_editor
{

// The shortest height of a content area: three rows plus the horizontal scrollbar. A tile's minimum counts it in.
float rv_editor_content_min_height();

// A scroll area (rv_editor_scroll_begin, both axes) on the code background under the code text, with the well's frame.
// `size` works as for rv_editor_scroll_begin. Always pair with rv_editor_content_end, whatever begin returned.
bool rv_editor_content_begin(const char *id, ImVec2 size, const rv_editor_theme &theme);
void rv_editor_content_end(const rv_editor_theme &theme);

// The empty state: one dim line of `text` where the first row would be. Column headers are the caller's and stay as
// they are with rows; in a table, call it after rv_editor_table_row and rv_editor_table_cell.
// `button`, when not null, is a button in the same row (a filter's reset); true when it was pressed.
bool rv_editor_content_empty_row(const char *text, const rv_editor_theme &theme, const char *button = nullptr);

// A titled section inside one content area (call between rv_editor_content_begin and _end). The title row is a band over
// the area's full inner width: `title` at the left, and the cursor left on the same row at the right, where the caller
// draws its controls (a field, buttons) in `controls_width` pixels. _end moves the cursor below the row. The section's
// minimum width is the title plus a gap plus the controls; an area narrower than it is the caller's to avoid.
float rv_editor_content_section_min_width(const char *title, float controls_width);
void rv_editor_content_section_begin(const char *title, float controls_width, const rv_editor_theme &theme);
void rv_editor_content_section_end(const rv_editor_theme &theme);

// The widest status word a record list shows, mark included: the width of the pinned status column.
float rv_editor_record_status_width(const char *const words[], int count);

struct rv_editor_record_style {
    bool selected = false;
    float status_width = 0.0f; // 0 = the width of this status alone
};

// What a record row did and where it is, in screen pixels. `status` is the pinned column's rectangle.
struct rv_editor_record_result {
    bool clicked = false;
    bool double_clicked = false;
    bool right_clicked = false;
    ImVec2 row_min, row_max;
    ImVec2 status_min, status_max;
};

// A record: exactly two lines, `name` then `path` dim, whole whatever their length; `status` (rv_editor_status, one line)
// pinned at the right edge of the visible area in a column of style.status_width. The row spans the content's width
// and selects as a whole.
rv_editor_record_result rv_editor_record_row(const char *id,
    const char *name,
    const char *path,
    const char *status,
    rv_editor_status_kind kind,
    const rv_editor_theme &theme,
    const rv_editor_record_style &style = {});

} // namespace rv_editor
