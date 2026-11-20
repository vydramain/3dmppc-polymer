// The status line under a control strip's buttons (Runtime Controls, Release Controls): a place two text lines high that is
// there in every state.

#pragma once

#include <string>

#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// The status's height: two text lines. Call inside a frame.
float rv_editor_strip_status_height();

// The least width in which `text` wraps whole into the status's two lines. Call inside a frame.
float rv_editor_strip_status_width_for(const std::string &text);

// A child window "##status", two text lines high and `width` wide (0 = the room left), the same rectangle for any text,
// empty included. The text wraps at words inside it, whole: never cut to "...", never scrolled. `tip` (the text itself
// when empty) shows on hover.
void rv_editor_strip_status(const std::string &text, float width, const rv_editor_theme &theme, const std::string &tip = {});

// The text the last rv_editor_strip_status call was given (a copy, kept for tests: the probe cannot read drawn text).
const std::string &rv_editor_strip_status_last();

} // namespace rv_editor
