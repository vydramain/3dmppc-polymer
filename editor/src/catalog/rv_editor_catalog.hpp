#pragma once

#include "imgui.h"

#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// Widget Catalog: every component in every state, drawn with the real
// theme, for checking the theme by eye. Draws into the current window, which
// scrolls once every group is in.
void rv_editor_catalog_draw(const rv_editor_theme &theme);

// The parts the twelve sections are made of, by file.
void rv_editor_catalog_colours(const rv_editor_theme &theme);   // _theme: palette, primitives
void rv_editor_catalog_icon_set(const rv_editor_theme &theme);  // _theme: file and tool icons
void rv_editor_catalog_buttons(const rv_editor_theme &theme);   // _buttons
void rv_editor_catalog_fields(const rv_editor_theme &theme);    // _fields
void rv_editor_catalog_headers(const rv_editor_theme &theme);   // _panes: headers, splitters, tiles
void rv_editor_catalog_lists(const rv_editor_theme &theme);     // _panes: tree, list, table
void rv_editor_catalog_tab_strips(const rv_editor_theme &theme); // _panes: tabs
void rv_editor_catalog_menus_dialogs(const rv_editor_theme &theme); // _status
void rv_editor_catalog_lamps(const rv_editor_theme &theme);     // _status: state lamps
void rv_editor_catalog_logs(const rv_editor_theme &theme);      // _status: the log
void rv_editor_catalog_transports(const rv_editor_theme &theme); // _status: the transport
void rv_editor_catalog_keys(const rv_editor_theme &theme);      // _more: checkboxes, diamonds, keys
void rv_editor_catalog_overflow(const rv_editor_theme &theme);  // _more: scrollbars, narrow transport
void rv_editor_catalog_code(const rv_editor_theme &theme);      // _more: code text
void rv_editor_catalog_game(const rv_editor_theme &theme);      // _more: the game frame
void rv_editor_catalog_cells(const rv_editor_theme &theme);     // _more: catalog cells, drop pocket
void rv_editor_catalog_type(const rv_editor_theme &theme);      // _more: fonts, contrast, thumbwheel

// Reserves a `size` item in the layout and returns its top-left corner.
ImVec2 rv_editor_catalog_reserve(ImVec2 size);

} // namespace rv_editor
