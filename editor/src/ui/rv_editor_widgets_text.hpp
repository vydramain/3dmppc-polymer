#pragma once

#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// Text components: labels in the theme's colours, section headers, separators and
// tooltips. Text is drawn by ImGui; the caller formats it first (rv_editor_text_format).

enum class rv_editor_label_kind {
    normal,  // theme.text
    dim,     // theme.text_disabled
    bright,  // theme.text_bright
    info,    // theme.code_blue: information, the manual's subheadings
    warning, // theme.warning
    error,   // theme.error
    on_selection, // theme.dark: text on the brass fill of a selected row
};

enum class rv_editor_label_layout {
    line,   // one line, never wrapped
    wrap,   // breaks at the window's edge
    bullet, // list item: a bullet, then wrapped text
};

// A label in `kind`'s colour, laid out as `layout` says.
void rv_editor_label(const char *text,
    rv_editor_label_kind kind,
    const rv_editor_theme &theme,
    rv_editor_label_layout layout = rv_editor_label_layout::line);

// A section header: ImGui's SeparatorText, a rule with the title on it.
void rv_editor_section(const char *title);

// A collapsible section, closed at first, its state kept per title by ImGui. Returns
// true while open; the caller draws the content then. No end call.
bool rv_editor_collapsing_section(const char *title);

// A horizontal rule, in a menu too.
void rv_editor_separator();

// Tooltip shown now: the caller decides what is hovered.
void rv_editor_tooltip(const char *text);

// Tooltip of the last item, shown while that item is hovered.
void rv_editor_item_tooltip(const char *text);

} // namespace rv_editor
