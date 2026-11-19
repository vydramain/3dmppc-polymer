// The message box: one fixed rectangle for a pane's messages and asks of any length.

#pragma once

#include <functional>
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

// The text window of a box alone, 2 lines high (rv_editor_message_text_height with its padding) under `id`: for a pane's
// description of 2 lines. Same look and rectangle as the box's own text.
float rv_editor_message_text_height();
void rv_editor_message_text(const char *id,
    const std::string &text,
    rv_editor_label_kind kind,
    const rv_editor_theme &theme,
    float min_width = 0.0f);

// The narrowest box: the widest of `button_rows` side by side plus 30 characters of text per line.
float rv_editor_message_box_min_width(const std::vector<std::vector<std::string>> &button_rows);

// The box across the content region's width. The text wraps at words inside an ImGui child (with ImGui's own bar,
// so the text rectangle stays fixed) on the code background; it scrolls vertically only when it is longer than 2 lines. `kind`:
// normal (an ask) draws in the code text colour, info and error in their severity colours. The buttons sit in one row aligned
// right, the row's place is kept with none. Returns the index of the button pressed this frame, else -1.
// `min_width` is the least the box is wide, past the room left in the window (a pane's constant minimum).
// `lead`, when given, draws items in the button row left of the buttons, a frame high, and is told the width it has.
// `button_tooltip`, when given, is shown over each button.
int rv_editor_message_box(const char *id,
    const std::string &text,
    rv_editor_label_kind kind,
    const std::vector<std::string> &buttons,
    const rv_editor_theme &theme,
    float min_width = 0.0f,
    const std::function<void(float)> &lead = nullptr,
    const char *button_tooltip = nullptr);

} // namespace rv_editor
