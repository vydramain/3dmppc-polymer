// The message strip and the text window of a pane's description.

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widget_item.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

// The text window alone, 2 lines high (rv_editor_message_text_height with its padding) under `id`: for a pane's
// description of 2 lines.
float rv_editor_message_text_height();
void rv_editor_message_text(const char *id,
    const std::string &text,
    rv_editor_label_kind kind,
    const rv_editor_theme &theme,
    float min_width = 0.0f);

// What a strip says: the text (empty: nothing to say, no strip), its kind and its buttons.
struct rv_editor_message_say {
    std::string text;
    rv_editor_label_kind kind = rv_editor_label_kind::normal;
    std::vector<std::string> buttons;
};

// The narrowest strip: the widest of `button_rows` side by side plus 30 characters of text per line, and `field_chars`
// characters of an input field that shares the button row (0: no field).
float rv_editor_message_strip_min_width(const std::vector<std::vector<std::string>> &button_rows, int field_chars = 0);

// The strip's input field, in the button row left of the buttons; Enter in it presses button 0 while that button is active.
// Its ImGui label is "##field". `focus` takes the keyboard focus for this frame (a question just asked).
struct rv_editor_message_strip_field {
    char *buf = nullptr;
    size_t size = 0;
    const char *hint = nullptr; // shown dim while the field is empty
    bool focus = false;
};

// The message strip: drawn only with a non-empty `text`, at the cursor under a content area; returns the pressed
// button's index, else -1. `width` 0: the room left. Height: see rv_editor_message_strip_height (0 with no text).
// With a `field` or a `lead` (drawn in the room left of the buttons, told its width) the strip is two rows: the text, then
// the field or lead and the buttons; pass `second_row` true to the height then. `button_tooltip`, when given, is shown
// over each enabled button.
int rv_editor_message_strip(const char *id,
    const std::string &text,
    rv_editor_label_kind kind,
    const std::vector<std::string> &buttons,
    const rv_editor_theme &theme,
    float width = 0.0f,
    const std::vector<rv_editor_state> *button_states = nullptr,
    const rv_editor_message_strip_field *field = nullptr,
    const std::function<void(float)> &lead = nullptr,
    const char *button_tooltip = nullptr,
    int max_lines = 3);
// The tallest a strip gets: max_lines lines of text (a longer one scrolls inside), and with `second_row` the row under them.
float rv_editor_message_strip_max_height(bool second_row = false, int max_lines = 3);
float rv_editor_message_strip_height(const std::string &text,
    const std::vector<std::string> &buttons,
    float width,
    bool second_row = false,
    int max_lines = 3);

} // namespace rv_editor
