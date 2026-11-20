// What the panes of the tiled kinds (Export, Candidate, Observe, Session, Build Result) say in their strip when no
// project is open.

#pragma once

#include <initializer_list>

#include "app/rv_editor_app.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

// What a pane says with no project open: "No project is open." and its Open Project button (button 0 sets
// app.open_folder_request). With a project the pane says what it has to.
rv_editor_message_say rv_editor_say_no_project();

// Registers the rectangle of the last text item as an item of its own, so a layout check sees how wide the text is
// (ImGui gives a text no id). Call right after the text.
void rv_editor_say_whole_text();

// The width of a shelf holding `labels` as buttons in one row: the pane's minimum is at least that wide. Call inside a frame.
float rv_editor_say_shelf_width(std::initializer_list<const char *> labels);

} // namespace rv_editor
