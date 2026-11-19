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
void rv_editor_catalog_colours(const rv_editor_theme &theme);       // _theme: palette, primitives
void rv_editor_catalog_icon_set(const rv_editor_theme &theme);      // _theme: file and tool icons
void rv_editor_catalog_buttons(const rv_editor_theme &theme);       // _buttons
void rv_editor_catalog_fields(const rv_editor_theme &theme);        // _fields
void rv_editor_catalog_text_samples(const rv_editor_theme &theme);  // _fields: text components
void rv_editor_catalog_headers(const rv_editor_theme &theme);       // _panes: headers, splitters, tile windows
void rv_editor_catalog_menu_samples();                              // _status: menus, context menu
void rv_editor_catalog_lamps(const rv_editor_theme &theme);         // _status: state lamps
void rv_editor_catalog_keys(const rv_editor_theme &theme);          // _more: checkboxes, diamonds, keys
void rv_editor_catalog_code(const rv_editor_theme &theme);          // _more: code text
void rv_editor_catalog_game(const rv_editor_theme &theme);          // _more: the game frame
void rv_editor_catalog_cells(const rv_editor_theme &theme);         // _more: catalog cells, drop pocket
void rv_editor_catalog_type(const rv_editor_theme &theme);          // _more: fonts, contrast, thumbwheel
void rv_editor_catalog_tree_list(const rv_editor_theme &theme);     // _views: tree and list in every state
void rv_editor_catalog_scroll(const rv_editor_theme &theme);        // _views: scrollbars both ways
void rv_editor_catalog_log(const rv_editor_theme &theme);           // _views: log rows by severity

// The template sections (R7-R17, in this order: tabs, shelf, list, records, message box, big ask, form, transport,
// picture, facts, status bar). Each has a state switcher and two shows of the template: a tile at its minimum and a roomy one.
int rv_editor_catalog_section_count();
const char *rv_editor_catalog_section_name(int section);
int rv_editor_catalog_state_count(int section);
const char *rv_editor_catalog_state_name(int section, int state);
int rv_editor_catalog_state(int section); // the state the switcher shows now
void rv_editor_catalog_set_state(int section, int state);

// The window size (padding included) of the template's tile at its minimum and of the roomy one. Call inside a frame.
ImVec2 rv_editor_catalog_tile_min(int section, const rv_editor_theme &theme);
ImVec2 rv_editor_catalog_tile_roomy(int section, const rv_editor_theme &theme);

// Draws the template in `state` over the whole content region of the current window (sized as one of the tiles above).
void rv_editor_catalog_tile_draw(int section, int state, const rv_editor_theme &theme);

// The whole section: title, the state switcher (one item per state, labelled with its name) and the two tiles, each a
// child window of exactly its tile size, drawn in the switcher's state.
void rv_editor_catalog_section_draw(int section, const rv_editor_theme &theme);

// Reserves a `size` item in the layout and returns its top-left corner.
ImVec2 rv_editor_catalog_reserve(ImVec2 size);

} // namespace rv_editor
