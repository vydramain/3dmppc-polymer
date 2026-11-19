// What the logs, Terminal, Problems and Search share: the measures of their rows (shelf, message box) and the content
// area that keeps its floor size, so a pane's minimum is the sum of what it draws.

#pragma once

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

// The tile minimum of a pane whose widest row is `content_w` wide and whose rows above the content area are `above_h`
// high: the window's padding, the rows and the content area at its floor.
rv_editor_size rv_editor_tiled_minimum(float content_w, float above_h);

// The content area under the rows above, filling what is left but never under its floor: `content_w` wide and the
// shortest height of a content area. Pair with rv_editor_tiled_area_end.
bool rv_editor_tiled_area_begin(const char *id, float content_w, const rv_editor_theme &theme);

// Ends the area and registers its whole frame as one item, so a tile smaller than the minimum is seen to cut it.
void rv_editor_tiled_area_end(const rv_editor_theme &theme);

} // namespace rv_editor
