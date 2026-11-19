// What the panes of the tiled kinds (Export, Candidate, Observe, Session, Build Result) say in their message box when no
// project is open.

#pragma once

#include <initializer_list>

#include "app/rv_editor_app.hpp"
#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// With no project open: the box "No project is open." with Open Project, `width` wide; true when it was drawn. With a
// project nothing is drawn and the pane says what it has to.
bool rv_editor_say_no_project(rv_editor_app &app, const rv_editor_theme &theme, float width);

// Registers the rectangle of the last text item as an item of its own, so a layout check sees how wide the text is
// (ImGui gives a text no id). Call right after the text.
void rv_editor_say_whole_text();

// The width of a shelf holding `labels` as buttons in one row: the pane's minimum is at least that wide. Call inside a frame.
float rv_editor_say_shelf_width(std::initializer_list<const char *> labels);

} // namespace rv_editor
