#pragma once

// A picture area: all the room left in the window, a dark backing, the frame aspect-fitted in it and the status or
// notes on a translucent band over its top. The band never takes height from the picture.
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

struct rv_editor_picture_rect {
    ImVec2 min, max;
};

// Opens the area from the cursor to the window's content edge on both axes, draws the dark backing and submits one
// item of that size (a button when clickable; *clicked tells a click). Returns the area. Pair with _end.
rv_editor_picture_rect
rv_editor_picture_begin(const char *id, const rv_editor_theme &theme, bool clickable, bool *clicked = nullptr);
void rv_editor_picture_end();

// Where a frame_w x frame_h picture goes inside area: the largest aspect-keeping rectangle, centred.
rv_editor_picture_rect rv_editor_picture_frame(const rv_editor_picture_rect &area, int frame_w, int frame_h);

// Draws over what the draw list holds: a translucent backing from the area's top, as wide as the area and as tall as
// text wrapped at the area's width, then the text in kind's colour. Empty text draws nothing and returns a
// zero-height band. Adds no item and moves no cursor. Returns the band.
rv_editor_picture_rect rv_editor_picture_overlay(std::string_view text,
    rv_editor_label_kind kind,
    const rv_editor_theme &theme,
    const rv_editor_picture_rect &area);

// The least area width at which every text wraps into at most two lines (0 for no texts).
float rv_editor_picture_min_width(const std::vector<std::string> &texts);

} // namespace rv_editor
