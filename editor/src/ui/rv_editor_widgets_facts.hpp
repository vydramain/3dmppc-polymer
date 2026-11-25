#pragma once

// A facts list: key and value rows in a content area (Catppuccin code background). Keys are a column pinned at the
// left edge; values are whole, however long, so the area scrolls both ways only when it overflows. A missing value
// shows "-". A right click on a row opens its menu with Copy, which puts the value on the clipboard.

#include "imgui.h"
#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// Opens the facts area; `size` works as for rv_editor_content_begin. Always pair with rv_editor_facts_end.
void rv_editor_facts_begin(const char *id, ImVec2 size, const rv_editor_theme &theme);

// The facts table alone, for a facts section inside a content area that holds more than facts. Always pair them.
void rv_editor_facts_table_begin();
void rv_editor_facts_table_end();

// Where a fact's key and value were drawn, in screen pixels; the value's rectangle is as wide as its whole text.
struct rv_editor_facts_row_result {
    ImVec2 key_min, key_max;
    ImVec2 value_min, value_max;
};

// One fact; `value` null or empty shows "-". The key column is as wide as the longest key.
rv_editor_facts_row_result rv_editor_facts_row(const char *key, const char *value, const rv_editor_theme &theme);

void rv_editor_facts_end(const rv_editor_theme &theme);

} // namespace rv_editor
