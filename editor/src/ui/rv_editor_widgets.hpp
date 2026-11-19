#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "layout/rv_editor_tile.hpp"
#include "ui/rv_editor_draw.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_icons.hpp"
#include "ui/rv_editor_widget_item.hpp"
#include "ui/rv_editor_widgets_status.hpp"
#include "ui/rv_editor_widgets_table.hpp"
#include "ui/rv_editor_widgets_text.hpp"
#include "ui/rv_editor_widgets_tile.hpp"

namespace rv_editor
{

// The editor's own widgets. Each returns what the matching ImGui call
// would: true when clicked or when its value changed. Behaviour comes from
// ImGui through rv_editor_item_add; the look comes from rv_editor_draw.

// --- buttons and toggles ------------------------------------------------------

bool rv_editor_button(const char *label, const rv_editor_theme &theme, const rv_editor_state &state = {});

// A framed card of `size` at the cursor: its whole frame is one click area. Returns true when clicked; a
// disabled card ignores clicks and shows state.disabled as its tooltip. The cursor ends below the card, so to
// draw its content call ImGui::GetItemRectMin() right after and draw with the window draw list (text only,
// no interactive items: they would take the clicks) or SetCursorScreenPos into the rect and restore it.
bool rv_editor_card(const char *id,
    ImVec2 size,
    const rv_editor_theme &theme,
    bool selected = false,
    const rv_editor_state &state = {});
bool rv_editor_icon_button(const char *id,
    rv_editor_icon_name icon,
    const rv_editor_theme &theme,
    const rv_editor_state &state = {});

// A square button of frame height with one coloured code (rv_editor_glyph)
// standing in for an icon; `tooltip` names what it does.
bool rv_editor_letter_button(const char *id,
    const char *code,
    uint32_t color,
    const char *tooltip,
    const rv_editor_theme &theme,
    const rv_editor_state &state = {});
// A letter_button that stays pressed while *on; a click flips *on.
bool rv_editor_letter_toggle(const char *id,
    const char *code,
    uint32_t color,
    const char *tooltip,
    bool *on,
    const rv_editor_theme &theme,
    const rv_editor_state &state = {});

// One row of a Toolchest across the pane's width: a coloured code (rv_editor_glyph)
// and the label, `tooltip` saying what it does (a disabled row says why instead).
bool rv_editor_command_button(const char *id,
    const char *code,
    uint32_t color,
    const char *label,
    const char *tooltip,
    const rv_editor_theme &theme,
    const rv_editor_state &state = {});

// A button that stays pressed while *on.
bool rv_editor_toggle(const char *label, bool *on, const rv_editor_theme &theme, const rv_editor_state &state = {});
bool rv_editor_checkbox(const char *label, bool *on, const rv_editor_theme &theme, const rv_editor_state &state = {});
// Widths a button and a check box with `label` take, for rv_editor_flow.
float rv_editor_button_width(const char *label);
float rv_editor_checkbox_width(const char *label);

// The width of the widest of `labels` as a button: a button whose label changes keeps it.
float rv_editor_button_width_for(std::initializer_list<const char *> labels);
// A button as wide as `label` needs but never wider than `max_width`: a longer label ends in dots.
bool rv_editor_button_clipped(const char *label,
    float max_width,
    const rv_editor_theme &theme,
    const rv_editor_state &state = {});
// A button showing `label`, as wide as the widest of `labels_for_width`.
bool rv_editor_button_fixed(const char *label,
    std::initializer_list<const char *> labels_for_width,
    const rv_editor_theme &theme,
    const rv_editor_state &state = {});

// Keeps the next item on the current row when `width` still fits in the window,
// and starts a new row otherwise, so a row of controls wraps in a narrow pane
// instead of running out of sight. Call it between items, like SameLine.
void rv_editor_flow(float width);

// Diamond radio: returns true when clicked; the caller owns which one is active.
bool rv_editor_radio(const char *label, bool active, const rv_editor_theme &theme, const rv_editor_state &state = {});

// --- fields -------------------------------------------------------------------
// Width comes from ImGui::SetNextItemWidth / PushItemWidth like any ImGui field.

// The states a field has beyond a button's.
struct rv_editor_field {
    rv_editor_state state;
    bool read_only = false;
    bool dirty = false;            // edited and not saved: a "*" marker
    const char *invalid = nullptr; // what is wrong: error frame, "!" marker, tooltip
};

bool rv_editor_text_field(const char *label,
    char *buf,
    size_t size,
    const rv_editor_theme &theme,
    const rv_editor_field &field = {});
// The ImGui flags a text area is drawn with: read-only as the field says, and always word-wrapped.
ImGuiInputTextFlags rv_editor_text_area_flags(const rv_editor_field &field);
// A multi-line field `lines` text lines tall (plus its frame padding), as wide as the
// item width; ImGui scrolls it. The markers sit on its first line.
bool rv_editor_text_area(const char *label,
    char *buf,
    size_t size,
    float lines,
    const rv_editor_theme &theme,
    const rv_editor_field &field = {});
// Int and 64-bit spinners: true on every change, by an arrow or by typing. The last ImGui
// item is the layout Dummy, so IsItemDeactivatedAfterEdit() does not describe the edit.
bool rv_editor_spinner(const char *label,
    int *value,
    int step,
    const rv_editor_theme &theme,
    const rv_editor_field &field = {});
bool rv_editor_spinner(const char *label,
    int64_t *value,
    int64_t step,
    const rv_editor_theme &theme,
    const rv_editor_field &field = {});
bool rv_editor_dropdown(const char *label,
    int *current,
    const char *const items[],
    int count,
    const rv_editor_theme &theme,
    const rv_editor_field &field = {});

// Plain text as a layout item of its own size and an empty label (ImGui text has no id, so layout tests and hover
// would not see it);
// `pad_y` is added above and below the text. `colour` 0 = the text colour.
void rv_editor_text_item(const char *text, uint32_t colour = 0, float pad_y = 0.0f);

// A read-only path: its label, then the whole path (a narrow row overflows), the same in a tooltip, and Copy in the
// context menu of the path.
void rv_editor_path_row(const char *label, const std::string &path, const rv_editor_theme &theme);

// What a path is shown as when it does not fit `width`: "..." and its END (the file name stays whole); a path that
// fits is returned unchanged.
std::string rv_editor_path_tail(const std::string &path, float width);

// A path inside `root` as it is relative to it ("export/web"); one outside it (or a sibling that only shares the
// prefix) stays as given.
std::string rv_editor_path_relative(const std::string &path, const std::string &root);

// The text a pane header / a tab shows for `title` in `width`: a path-like title (has a '/') keeps its tail.
std::string rv_editor_pane_header_title(const char *title, float width);

// What rv_editor_path_field drew last, for tests.
struct rv_editor_path_probe {
    std::string shown;   // the text on screen
    std::string tooltip; // the full path
    bool editing = false;
};

// For tests: what the path field with ImGui item id `id` drew last; nullptr when it has not drawn. Kept by every
// rv_editor_path_field, whether or not the caller passes a probe.
const rv_editor_path_probe *rv_editor_path_probe_of(uint32_t id);

// A text field for a path: unfocused it shows the tail (relative to `root` when inside it) with the full path in a
// tooltip; focused it is a normal input field over the whole value.
bool rv_editor_path_field(const char *label,
    char *buf,
    size_t size,
    const std::string &root,
    const rv_editor_theme &theme,
    const rv_editor_field &field = {},
    rv_editor_path_probe *probe = nullptr);

// Whole pixels for a measured height or chrome: rounded up, so a fraction never cuts the last row off.
int32_t rv_editor_px_ceil(float px);

// --- layers: shelf and well ----------------------------------------------------
// Every pane reads header (dark), shelf (its buttons), well (its content).

// A raised strip across the available width for a row (or a few) of buttons:
// whatever is drawn between begin and end, SameLine/rv_editor_flow included.
// Height follows the content; call once per shelf. Always pair with the end call.
void rv_editor_shelf_begin(const char *id, const rv_editor_theme &theme);
void rv_editor_shelf_end();

// A child region with the inset background and a sunken bevel, for a pane's
// content. `size` works as for BeginChild: 0 fills what is left. Unless `flags`
// has NoScrollbar, the content scrolls in a Motif area (rv_editor_scroll_begin);
// in scroll mode only HorizontalScrollbar of `flags` is used (it allows the horizontal bar).
// Always pair with the end call.
bool rv_editor_well_begin(const char *id, ImVec2 size, const rv_editor_theme &theme, ImGuiWindowFlags flags = 0);
void rv_editor_well_end();

// --- trees --------------------------------------------------------------------

// How a tree row looks and opens. `open` is set this frame and opens or closes
// the branch; `default_open` applies on first use only.
struct rv_editor_tree_style {
    bool leaf = false;    // no arrow
    bool no_push = false; // no children: ImGui pushes no id, so nothing to pop
    bool selected = false;
    bool dim = false; // label in text_disabled; the caller says why (hidden, left out)
    bool default_open = false;
    bool open_on_double_click = false; // a double click opens too, as the arrow does
    bool lines = false;                // lines down to the children
    std::optional<bool> open;
};

// What the row did. `open` is true when the caller draws the children and calls
// rv_editor_tree_pop. Drag source, drop target and context menu come after this
// call; the row is the last item it leaves. A frozen look reports no click or toggle.
struct rv_editor_tree_result {
    bool open = false;
    bool clicked = false;       // left click on the label, not the arrow
    bool right_clicked = false; // right click on the row
    bool toggled = false;       // the arrow or a double click opened or closed the branch
};

// A tree row: ImGui's TreeNodeEx in the theme's colours. Opens by the arrow, or by
// a double click when the style says so.
rv_editor_tree_result rv_editor_tree_row(const char *id,
    const char *label,
    const rv_editor_theme &theme,
    const rv_editor_tree_style &style = {},
    const rv_editor_state &state = {});
void rv_editor_tree_pop();

// --- lists --------------------------------------------------------------------

// How a list row looks. `height` 0 is the usual line height. `dim` only dims the label
// (text_disabled); a dim row is not disabled and has no tooltip. `span_columns` spans
// every table column; `overlap` lets the next items overlap the row; `whole` keeps a label wider than the row whole
// (it is clipped by the cell, the caller gives the tooltip).
struct rv_editor_list_style {
    bool selected = false;
    bool dim = false;
    bool span_columns = false;
    bool overlap = false;
    float height = 0.0f;
    bool whole = false;
};

// What the row did. A frozen look or a disabled row reports nothing. Order: the second
// press of a double click sets double_clicked, and clicked comes on its release, so a
// double click gives both.
struct rv_editor_list_result {
    bool clicked = false;
    bool double_clicked = false;
    bool right_clicked = false;
};

// A list's frame is a well; its scroll area (rv_editor_scroll_begin) gives it the
// Motif scrollbar. `size` works as for rv_editor_well_begin. Rows go between the two
// calls; always pair them.
bool rv_editor_list_begin(const char *id, ImVec2 size, const rv_editor_theme &theme);
void rv_editor_list_end();

// A list row: ImGui's Selectable in the theme's colours, spanning table columns and
// letting the next items overlap it as the style says. Drag source and context menu
// come after this call; the row is the last item it leaves.
rv_editor_list_result rv_editor_list_row(const char *id,
    const char *label,
    const rv_editor_theme &theme,
    const rv_editor_list_style &style = {},
    const rv_editor_state &state = {});

// --- panes --------------------------------------------------------------------
// Tables are the editor's own (rv_editor_table_begin); the pane pieces below
// are the ones ImGui has no public form of.

// Slanted folder tabs, one per label, each as wide as its whole label: the front tab in brass, the rest
// behind it. `max_tab_width` is unused. Returns true when a click moved *active.
// Non-null `pressed` gets the index of the tab the left button went down on
// this frame, for a caller that tells a drag from a click without repeating
// this layout.
// A strip wider than its area scrolls: the mouse wheel over it and `<` `>` buttons; `v` opens a list of every tab.
// There is no scrollbar, and an active tab that changed is scrolled into view.
// A non-null `probe` gets what the strip drew, for tests.
struct rv_editor_tab_probe {
    struct rect {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // screen pixels
    };
    std::vector<rect> tabs;           // every tab, scrolled out of view or not
    float strip_x0 = 0, strip_x1 = 0; // the visible part of the strip
    float scroll_x = 0;
    bool scrollbar = false; // a horizontal scrollbar is drawn
    float strip_y0 = 0, strip_y1 = 0; // the row of the strip
    rect prev, next, list;            // the `<` `>` `v` buttons; all zero while the tabs fit
    bool list_open = false;           // the list of every tab is open
    std::vector<rect> list_items;     // its entries, in tab order, while it is open
    std::vector<std::string> shown;   // the text drawn on each tab
};

// The height of the strip: one tab; a tile reserves it whether or not it has several tabs.
float rv_editor_tab_strip_height();

bool rv_editor_tab_strip(const char *id,
    const char *const labels[],
    int count,
    int *active,
    const rv_editor_theme &theme,
    const rv_editor_state &state = {},
    int *pressed = nullptr,
    float max_tab_width = 240.0f,
    rv_editor_tab_probe *probe = nullptr);

// A scrolling area with Motif scrollbars instead of ImGui's: arrow boxes
// at both ends, a sunken trough, a raised thumb with a grip. A bar appears once
// the content does not fit. `size` works as for BeginChild; `horizontal` also
// allows the horizontal bar. Always pair with rv_editor_scroll_end, whatever
// begin returned, as with BeginChild/EndChild.
bool rv_editor_scroll_begin(const char *id,
    ImVec2 size,
    bool horizontal = false,
    ImGuiChildFlags child_flags = ImGuiChildFlags_None);
void rv_editor_scroll_end(const rv_editor_theme &theme);
// The innermost open area's content width as of the last frame (0 before the first).
float rv_editor_scroll_content_width();

// Draggable bar between two panes. rv_editor_axis::x: a vertical bar `length`
// tall that moves along X. Keeps *a >= min_a and *b >= min_b; returns true on change.
bool rv_editor_splitter(const char *id,
    rv_editor_axis axis,
    float length,
    float *a,
    float *b,
    float min_a,
    float min_b,
    const rv_editor_theme &theme,
    const rv_editor_state &state = {});

// What a click on a pane header's boxes asked for.
enum class rv_editor_header_action {
    none,
    close,
    maximize,
};

// The width a leaf needs for rv_editor_pane_header to show `title` whole between its X and M boxes.
float rv_editor_pane_header_min_width(const char *title, const rv_editor_theme &theme);

// Full-width pane title bar: stippled when active, dimmed when not. With
// `controls` it carries a close box (X) on the left and a maximize box (M) on
// the right, each a button with every state, and returns the one clicked.
// Non-null `title_pressed` gets whether the left button went down this frame
// over the title area itself (not the boxes), for a caller that tells a drag
// from a click without repeating this layout.
rv_editor_header_action rv_editor_pane_header(const char *title,
    bool active,
    const rv_editor_theme &theme,
    bool controls = false,
    const rv_editor_state &state = {},
    bool *title_pressed = nullptr);

// --- the tiled workspace ------------------------------------------------------

// A tile drag in progress: press-and-hold on a header or a tab, then
// move past a few pixels to show a drop-zone preview; released over a valid
// zone, it moves the same pane instance with rv_editor_tile_move.
struct rv_editor_tile_drag {
    bool armed = false;    // pressed on a header/tab, threshold not yet crossed
    bool dragging = false; // threshold crossed: previewing, moves on release
    rv_editor_pane_id pane = rv_editor_tile_none;
    uint32_t from_leaf = rv_editor_tile_none;
    ImVec2 press{ 0.0f, 0.0f }; // where the button went down
};

// A change to the tree, recorded while drawing and applied after it.
struct rv_editor_tile_action {
    enum class op {
        none,
        split,
        set_kind,
        maximize,
        close,
        close_leaf
    } what = op::none;
    uint32_t leaf = rv_editor_tile_none; // the leaf acted on (split, maximize)
    rv_editor_pane_id pane = rv_editor_tile_none;
    rv_editor_pane_kind kind = rv_editor_pane_kind::empty;
    rv_editor_tile_dock dock = rv_editor_tile_dock::tab;
};

// What the window shows: the pane registry and the tile tree over it.
// Views only; no model lives here.
struct rv_editor_workspace {
    rv_editor_pane_registry panes;
    rv_editor_layout layout;
    uint32_t focused_leaf = rv_editor_tile_none;
    // A leaf whose content takes the keyboard at its next draw (Focus Next Pane).
    uint32_t focus_request = rv_editor_tile_none;
    // What the owner says about panes this frame, filled before each draw: a
    // title in place of the kind's (a code tile names its file), and the least
    // content size a pane needs (the Game's frame at 1x).
    std::map<rv_editor_pane_id, std::string> titles;
    std::map<rv_editor_pane_id, rv_editor_size> minimums;
    // Each node's rectangle at the last draw, by node index; a splitter was dragged.
    std::vector<rv_editor_rect> rects;
    bool dragged = false;
    rv_editor_tile_drag drag;
    // A pane's request waits for a frame the menu left empty.
    rv_editor_tile_action pending;
};

// Arms a tile drag when `title_pressed` says the header (rv_editor_pane_header)
// just saw the left button go down over its title; the dragged pane is the
// leaf's active tab. No-op once a drag is already armed or in progress.
void rv_editor_tile_drag_header(rv_editor_workspace &ws, uint32_t leaf, bool title_pressed);

// Arms a tile drag when `pressed_tab` (as rv_editor_tab_strip's `pressed` out
// param named it, or -1) says the left button just went down on that tab.
void rv_editor_tile_drag_tabs(rv_editor_workspace &ws, uint32_t leaf, int pressed_tab);

// Whether a drag is armed or in progress.
bool rv_editor_tile_drag_running(const rv_editor_workspace &ws);

// The drag's frame, called only while a drag runs: asks whether the button was
// released, Escape pressed, the threshold passed and the pointer is over a zone,
// then runs the drop, the cancel, the start or the preview. A release over a
// valid zone moves the pane and returns the move's code, else RV_OK; `error`
// names a refused move. Call once per frame, after every leaf has been placed,
// with each node's rectangle from that placement.
int rv_editor_tile_drag_frame(rv_editor_workspace &ws,
    const std::vector<rv_editor_rect> &rect_of,
    const rv_editor_theme &theme,
    std::string &error);

// Draws one pane's content into the current ImGui window. `context` is what the
// caller handed rv_editor_workspace_draw: the models the panes are views of.
using rv_editor_pane_draw_fn = void (*)(void *context,
    rv_editor_pane_id pane,
    rv_editor_pane_kind kind,
    const rv_editor_theme &theme);

// Asked before a pane leaves the tree (closed, or turned into another kind).
// False keeps it: the owner asks the user and removes it itself later.
// nullptr closes every pane at once.
using rv_editor_pane_close_fn = bool (*)(void *context, rv_editor_pane_id pane);

// Title of a pane kind as the tab and header show it.
const char *rv_editor_pane_title(rv_editor_pane_kind kind);

// Fills `area` (screen pixels) of the current window with the workspace, a child
// window of its own. A splitter drag changes its
// split's ratio.
// Returns the code of a drop that ended this frame's tile drag, else RV_OK; `error` names a refused drop.
int rv_editor_workspace_draw(rv_editor_workspace &ws,
    const rv_editor_theme &theme,
    rv_editor_pane_draw_fn draw_pane,
    rv_editor_pane_close_fn close_pane,
    void *context,
    rv_editor_rect area,
    std::string &error);

// --- menus ----------------------------------------------------------------------
// Menus, popups and context menus are the editor's over ImGui's calls, in the
// theme's menu colours. OpenPopup stays with the caller: it has no look to give.

// Menus hover in brass, so an open menu keeps its pressed look under the
// pointer. The components below use this pair; a popup not yet on them calls it itself.
void rv_editor_menu_style_push();
void rv_editor_menu_style_pop();

// Main menu bar; end only when begin returned true (a false begin clears the style).
bool rv_editor_main_menu_bar_begin();
void rv_editor_main_menu_bar_end();

// Menu bar of the current window (it needs ImGuiWindowFlags_MenuBar); end only when begin returned true.
bool rv_editor_menu_bar_begin();
void rv_editor_menu_bar_end();

// A menu; `why_not` (non-null) disables it and shows the reason on hover. End only when begin returned true.
bool rv_editor_menu_begin(const char *label, const char *why_not);
void rv_editor_menu_end();

// A popup opened with OpenPopup(id); end only when begin returned true.
bool rv_editor_popup_begin(const char *id);
void rv_editor_popup_end();

// A context menu opened by a right click on `id`; nullptr means the last item. End only when begin returned true.
bool rv_editor_context_menu_begin(const char *id);
void rv_editor_context_menu_end();

// Right-click menu of the current window's empty space; end only when begin returned true.
bool rv_editor_window_context_menu_begin(const char *id);
void rv_editor_window_context_menu_end();

// A menu item that is disabled with its reason shown on hover.
bool rv_editor_menu_item(const char *label, const char *shortcut, const char *why_not);
// A menu item with a check mark while *on (not null); a click flips *on. `why_not` as for rv_editor_menu_item.
bool rv_editor_menu_check(const char *label, const char *shortcut, bool *on, const char *why_not);
// A choice item marked while `selected`; a click returns true, the caller sets the choice. `why_not` as for menu_item.
bool rv_editor_menu_choice(const char *label, const char *shortcut, bool selected, const char *why_not);

} // namespace rv_editor
