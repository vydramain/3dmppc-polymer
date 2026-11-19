// The big ask: a card that replaces a tile's content.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "imgui.h"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

struct rv_editor_mode_card_spec {
    std::string title;
    std::string text;    // scrolls inside its own area on a Catppuccin background
    std::function<void()> body; // when set, drawn inside that area in place of `text` (a form, a list)
    std::string message; // one line, a constant place; empty text still keeps the place
    rv_editor_label_kind message_kind = rv_editor_label_kind::error;
    std::vector<std::string> buttons; // one row, left to right
};

// The smallest size a card with these buttons draws whole: the title, 2 lines of text, the message line and the button
// row high, as wide as the buttons side by side. Call inside a frame (it measures the current font and style).
ImVec2 rv_editor_mode_card_min_size(const std::vector<std::string> &buttons, const rv_editor_theme &theme);

// A card over the whole content region: the title, the text, the message line and the buttons. Returns the index of the
// button pressed this frame, else -1. Always pair with rv_editor_mode_card_end.
int rv_editor_mode_card_begin(const char *id, const rv_editor_mode_card_spec &spec, const rv_editor_theme &theme);
void rv_editor_mode_card_end();

} // namespace rv_editor
