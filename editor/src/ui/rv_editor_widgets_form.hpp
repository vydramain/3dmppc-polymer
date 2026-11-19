// The form: a label column as wide as the longest label, fields to its right, an error line under each field that can
// fail, and a button row at the bottom. Nothing is cut: the minimum is computed from the longest text.

#pragma once

#include <cstddef>
#include <vector>

#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

// What the form needs to know about a row before it is drawn: for the label column and the minimum.
struct rv_editor_form_row_desc {
    const char *label;
    const char *value; // what the field holds now (may be empty)
    const char *hint;  // shown dim when the value is empty (may be nullptr)
    bool can_fail;     // the row reserves an error line under its field
};

// Content width / height (inside the window padding) the form needs: label column + longest value or hint + frame
// padding; every row, every error line of a can_fail row and the button row. Call inside a frame (font metrics).
float rv_editor_form_min_width(const std::vector<rv_editor_form_row_desc> &rows);
float rv_editor_form_min_height(const std::vector<rv_editor_form_row_desc> &rows);

// Opens the form: the label column takes the longest label of `rows`.
void rv_editor_form_begin(const std::vector<rv_editor_form_row_desc> &rows, const rv_editor_theme &theme);

// One row: the label, then a text field right of the column whose ImGui id label is "##" + label (width >= the
// value, or the hint when empty). `error` (nullptr = none) is drawn under the field when `can_fail`; the line is
// reserved for a can_fail row either way.
bool rv_editor_form_row(const char *label,
    char *buf,
    size_t size,
    const char *hint,
    bool can_fail,
    const char *error,
    const rv_editor_theme &theme,
    const rv_editor_field &field = {});

// Moves to the button row at the bottom of the form (the same place whatever the errors); draw the buttons after it.
void rv_editor_form_buttons_begin(const rv_editor_theme &theme);

void rv_editor_form_end();

} // namespace rv_editor
