// What the logs, Terminal, Problems and Search share: the measures of their rows (shelf, message box) and the content
// area that keeps its floor size, so a pane's minimum is the sum of what it draws.

#pragma once

#include <initializer_list>

#include "imgui.h"
#include "layout/rv_editor_tile.hpp"
#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// A shelf of one frame-high row and the spacing under it. Call inside a frame.
float rv_editor_tiled_shelf_block();

// The message box and the spacing under it.
float rv_editor_tiled_box_block();

// A shelf's width round a row of `items` px: its padding on both sides.
float rv_editor_tiled_shelf_width(float items);

// The shortest frame of a content area that shows three rows of text whole, and of one that shows `rows` rows each
// `pitch` high (a row and the spacing under it) whole.
float rv_editor_tiled_floor();
float rv_editor_tiled_rows_floor(float rows, float pitch);

// The pitch of a table row that holds a button: the frame, the cell's padding and the spacing.
float rv_editor_tiled_table_pitch();

// The tile minimum of a pane whose widest row is `content_w` wide and whose rows above the content areas are `above_h`
// high: the window's padding, the rows and the content areas, each at its floor (`floors`, one per area top to bottom;
// none: one area at rv_editor_tiled_floor), one spacing apart.
rv_editor_size rv_editor_tiled_minimum(float content_w, float above_h, std::initializer_list<float> floors = {});

// The content area under the rows above: `content_w` wide, `floors` the floors of this area and the ones still to come,
// none: this is the only one. The room left over the floors is shared equally by those areas. `facts` opens it as a facts
// list (rv_editor_facts_row). `height` > 0 takes that much at most instead of a share (never under the floor). Pair with
// rv_editor_tiled_area_end. `bottom` > 0 is the height of a message strip that follows the area: the area
// loses that much and the spacing above the strip from its bottom only.
bool rv_editor_tiled_area_begin(const char *id,
    float content_w,
    const rv_editor_theme &theme,
    std::initializer_list<float> floors = {},
    bool facts = false,
    float height = 0.0f,
    float bottom = 0.0f);

// Ends the area and registers its whole frame as one item, so a tile smaller than the minimum is seen to cut it.
void rv_editor_tiled_area_end(const rv_editor_theme &theme);

} // namespace rv_editor
