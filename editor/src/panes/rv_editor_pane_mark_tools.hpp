// The row "note field + Mark Moment / Capture Frame / Report Issue [+ Restart]" shared by the Session
// Toolchest (with Restart) and Runtime Controls (without it).

#pragma once

#include <vector>

#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

struct rv_editor_app;
struct rv_editor_ui_port;

// A tile's screen rectangle.
struct rv_editor_tile_rect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// Draws the row at the cursor, filling the content region's width, and returns the small tiles' rectangles in
// order: Mark, Capture, Report [, Restart].
std::vector<rv_editor_tile_rect>
rv_editor_mark_tools(rv_editor_app &app, const rv_editor_theme &theme, const rv_editor_ui_port &ui, bool with_restart);

// The narrowest the row can be drawn: a 10em field and the small tiles with their gaps.
float rv_editor_mark_tools_min_width(const rv_editor_theme &theme, bool with_restart);

} // namespace rv_editor
