// The message box: one fixed rectangle for a pane's messages and asks of any length.

#pragma once

#include <string>
#include <vector>

#include "imgui.h"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

// The box's height, the same in every state: exactly 2 text lines on Catppuccin (with the area's own padding and
// frame) and under them one button row a frame high. Call inside a frame (it measures the current font and style).
float rv_editor_message_box_height();

// The narrowest box: the widest of `button_rows` side by side plus 30 characters of text per line.
float rv_editor_message_box_min_width(const std::vector<std::vector<std::string>> &button_rows);

// The box across the content region's width. The text wraps at words inside an ImGui child (with ImGui's own bar,
// so the text rectangle stays fixed) on the code background; it scrolls vertically only when it is longer than 2 lines. `kind`:
// normal (an ask) draws in the code text colour, info and error in their severity colours. The buttons sit in one row aligned
// right, the row's place is kept with none. Returns the index of the button pressed this frame, else -1.
// `min_width` is the least the box is wide, past the room left in the window (a pane's constant minimum).
int rv_editor_message_box(const char *id,
    const std::string &text,
    rv_editor_label_kind kind,
    const std::vector<std::string> &buttons,
    const rv_editor_theme &theme,
    float min_width = 0.0f);

} // namespace rv_editor
