// What Files' two files share: the questions (rv_editor_pane_files_ask.cpp) and the tree (rv_editor_pane_files.cpp).

#pragma once

#include <filesystem>
#include <string>

#include "imgui.h"
#include "panes/rv_editor_panes.hpp"

namespace rv_editor
{

// The directory a new entry goes into: `sel` when it is a directory, its parent when it is a file, the root otherwise.
std::filesystem::path rv_editor_files_target_dir(rv_editor_app &app, const std::filesystem::path &sel);

// Asks a question of `view`: the dialog, the name it starts with, and for a delete what goes (listed once, now).
void rv_editor_files_ask(rv_editor_files_view &view,
    rv_editor_files_view::rv_editor_files_dialog dialog,
    const std::filesystem::path &target,
    const std::string &name);

// The widest row of the tile: the shelf, or the strip with its name field. Call inside a frame.
float rv_editor_files_content_width();

// The size the card of a folder's delete needs. Call inside a frame.
ImVec2 rv_editor_files_card_min_size();

// The card of a folder's delete in place of the tile's content; false (nothing drawn) when no such question is open.
bool rv_editor_files_card(rv_editor_app &app, rv_editor_pane_id pane, rv_editor_files_view &view, const rv_editor_theme &theme);

// The strip under the tree: a short question, the project's absence, or nothing (height 0). Its height in `width`, and it
// drawn there.
float rv_editor_files_strip_height(rv_editor_app &app, rv_editor_files_view &view, float width);
void rv_editor_files_strip(rv_editor_app &app,
    rv_editor_pane_id pane,
    rv_editor_files_view &view,
    float width,
    const rv_editor_theme &theme);

// The shelf: new file and folder, rename, delete and refresh.
void rv_editor_files_shelf(rv_editor_app &app,
    rv_editor_pane_id pane,
    rv_editor_files_view &view,
    const rv_editor_ui_port &ui,
    const rv_editor_theme &theme);

} // namespace rv_editor
