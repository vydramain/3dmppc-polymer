#pragma once

#include <cstddef>
#include <map>
#include <string>

#include "layout/rv_editor_tile.hpp"
#include "ui/rv_editor_draw.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_icons.hpp"
#include "ui/rv_editor_widget_item.hpp"

namespace rv_editor
{

// The editor's own widgets (UI-03). Each returns what the matching ImGui call
// would: true when clicked or when its value changed. Behaviour comes from
// ImGui through rv_editor_item_add; the look comes from rv_editor_draw.

// --- buttons and toggles ------------------------------------------------------

bool rv_editor_button(const char *label, const rv_editor_theme &theme, const rv_editor_state &state = {});
bool rv_editor_icon_button(const char *id, rv_editor_icon_name icon, const rv_editor_theme &theme,
    const rv_editor_state &state = {});
// A square transport button: a pixel picture in `color` over its label, every
// one the same size (rv_editor_tool_button_width). The tooltip names `shortcut`
// (nullptr: none); a disabled button dims the picture and its tooltip says why.
bool rv_editor_tool_button(const char *label, rv_editor_glyph glyph, uint32_t color, const char *shortcut,
    const rv_editor_theme &theme, const rv_editor_state &state = {});
float rv_editor_tool_button_width(const char *label);

// A square button of frame height with one coloured letter standing in for an
// icon (editor/docs/icons.md); `tooltip` names what it does.
bool rv_editor_letter_button(const char *id, char letter, uint32_t color, const char *tooltip,
    const rv_editor_theme &theme, const rv_editor_state &state = {});

// A button that stays pressed while *on.
bool rv_editor_toggle(const char *label, bool *on, const rv_editor_theme &theme, const rv_editor_state &state = {});
bool rv_editor_checkbox(const char *label, bool *on, const rv_editor_theme &theme, const rv_editor_state &state = {});
// Widths a button and a check box with `label` take, for rv_editor_flow.
float rv_editor_button_width(const char *label);
float rv_editor_checkbox_width(const char *label);

// Keeps the next item on the current row when `width` still fits in the window,
// and starts a new row otherwise, so a row of controls wraps in a narrow pane
// instead of running out of sight (UI-05). Call it between items, like SameLine.
void rv_editor_flow(float width);

// Diamond radio: returns true when clicked; the caller owns which one is active.
bool rv_editor_radio(const char *label, bool active, const rv_editor_theme &theme, const rv_editor_state &state = {});

// --- fields -------------------------------------------------------------------
// Width comes from ImGui::SetNextItemWidth / PushItemWidth like any ImGui field.

// The states a field has beyond a button's (UI-04).
struct rv_editor_field
{
    rv_editor_state state;
    bool read_only = false;
    bool dirty = false;             // edited and not saved: a "*" marker
    const char *invalid = nullptr;  // what is wrong: error frame, "!" marker, tooltip
};

bool rv_editor_text_field(const char *label, char *buf, size_t size, const rv_editor_theme &theme,
    const rv_editor_field &field = {});
bool rv_editor_spinner(const char *label, int *value, int step, const rv_editor_theme &theme,
    const rv_editor_field &field = {});
bool rv_editor_dropdown(const char *label, int *current, const char *const items[], int count,
    const rv_editor_theme &theme, const rv_editor_field &field = {});

// A read-only path: its label, then the path itself, cut at the front with
// "..." when it does not fit (the tail names the file), the whole of it in a
// tooltip, and a Copy button that puts it on the clipboard.
void rv_editor_path_row(const char *label, const std::string &path, const rv_editor_theme &theme);

// --- panes --------------------------------------------------------------------
// Trees, lists and tables are ImGui's own (TreeNodeEx, BeginListBox, BeginTable)
// in the theme's colours; these are the pane pieces ImGui has no public form of.

// Slanted folder tabs (UI-02), one per label: the front tab in brass, the rest
// behind it. Returns true when a click moved *active.
bool rv_editor_tab_strip(const char *id, const char *const labels[], int count, int *active,
    const rv_editor_theme &theme, const rv_editor_state &state = {});

// A scrolling area with Motif scrollbars (UI-02) instead of ImGui's: arrow boxes
// at both ends, a sunken trough, a raised thumb with a grip. A bar appears once
// the content does not fit. `size` works as for BeginChild; `horizontal` also
// allows the horizontal bar. Always pair with rv_editor_scroll_end, whatever
// begin returned, as with BeginChild/EndChild.
bool rv_editor_scroll_begin(const char *id, ImVec2 size, bool horizontal = false,
    ImGuiChildFlags child_flags = ImGuiChildFlags_None);
void rv_editor_scroll_end(const rv_editor_theme &theme);

// Draggable bar between two panes. rv_editor_axis::x: a vertical bar `length`
// tall that moves along X. Keeps *a >= min_a and *b >= min_b; returns true on change.
bool rv_editor_splitter(const char *id, rv_editor_axis axis, float length, float *a, float *b, float min_a, float min_b,
    const rv_editor_theme &theme, const rv_editor_state &state = {});

// What a click on a pane header's boxes asked for.
enum class rv_editor_header_action
{
    none,
    close,
    maximize,
};

// Full-width pane title bar: stippled when active, dimmed when not. With
// `controls` it carries a close box (X) on the left and a maximize box (M) on
// the right, each a button with every state, and returns the one clicked.
rv_editor_header_action rv_editor_pane_header(const char *title, bool active, const rv_editor_theme &theme,
    bool controls = false, const rv_editor_state &state = {});

// --- the tiled workspace ------------------------------------------------------

// What the window shows: the pane registry and the tile tree over it
// (docs/adr/0002-tiling.md). Views only; no model lives here.
struct rv_editor_workspace
{
    rv_editor_pane_registry panes;
    rv_editor_layout layout;
    uint32_t focused_leaf = rv_editor_tile_none;
    // What the owner says about panes this frame, filled before each draw: a
    // title in place of the kind's (a code tile names its file), and the least
    // content size a pane needs (the Game's frame at 1x, LAY-03).
    std::map<rv_editor_pane_id, std::string> titles;
    std::map<rv_editor_pane_id, rv_editor_size> minimums;
};

// Draws one pane's content into the current ImGui window. `context` is what the
// caller handed rv_editor_workspace_draw: the models the panes are views of.
using rv_editor_pane_draw_fn = void (*)(void *context, rv_editor_pane_id pane, rv_editor_pane_kind kind,
    const rv_editor_theme &theme);

// Asked before a pane leaves the tree (closed, or turned into another kind).
// False keeps it: the owner asks the user and removes it itself later
// (docs/adr/0002-tiling.md). nullptr closes every pane at once.
using rv_editor_pane_close_fn = bool (*)(void *context, rv_editor_pane_id pane);

// Title of a pane kind as the tab and header show it.
const char *rv_editor_pane_title(rv_editor_pane_kind kind);

// Fills `area` (screen pixels) of the current window with the workspace, a child
// window of its own. A splitter drag changes its
// split's ratio.
void rv_editor_workspace_draw(rv_editor_workspace &ws, const rv_editor_theme &theme, rv_editor_pane_draw_fn draw_pane,
    rv_editor_pane_close_fn close_pane, void *context, rv_editor_rect area);

// --- status, log, transport, dialogs --------------------------------------------
// Menus and context menus are ImGui's own (BeginMenuBar, BeginMenu, MenuItem,
// BeginPopupContextItem) in the theme's colours.

// Menus hover in brass, so an open menu keeps its pressed look under the
// pointer. Every menu bar and popup menu is drawn between these two calls.
void rv_editor_menu_style_push();
void rv_editor_menu_style_pop();

enum class rv_editor_status_kind
{
    idle,
    busy,
    ok,
    warning,
    error,
};

enum class rv_editor_severity
{
    info,
    warning,
    error,
};

// Status bar: one sunken field per text, left to right; the first field takes
// the width the others leave.
void rv_editor_status_bar(const char *const fields[], int count, const rv_editor_theme &theme);

// Lamp and label; each kind has its own symbol as well as its own colour.
void rv_editor_status(const char *label, rv_editor_status_kind kind, const rv_editor_theme &theme);

// A log's text area: Mocha base under Mocha text, with the Motif scrollbars of
// rv_editor_scroll_begin. Rows go between the two calls; always pair them.
bool rv_editor_log_begin(const char *id, ImVec2 size, const rv_editor_theme &theme);
void rv_editor_log_end(const rv_editor_theme &theme);

// One line of process output inside a log area: time and source dimmed, the
// severity as a coloured tag (INF, WRN, ERR), the text in the log's colour.
void rv_editor_log_row(const char *time, const char *source, rv_editor_severity severity, const char *text,
    const rv_editor_theme &theme);

// Why each transport action is unavailable; nullptr means available.
struct rv_editor_transport_state
{
    const char *build;
    const char *run;
    const char *pause;
    const char *step;
    const char *stop;
    const char *reload;
    bool resume = false; // the machine is paused: Run reads Resume
};

// Which transport action was clicked this frame.
struct rv_editor_transport_actions
{
    bool build;
    bool run;
    bool pause;
    bool step;
    bool stop;
    bool reload;
};

rv_editor_transport_actions rv_editor_transport_bar(const rv_editor_transport_state &state,
    const rv_editor_theme &theme);

// Modal dialog with a pane header for its title. Open it with ImGui::OpenPopup(title);
// call rv_editor_dialog_end() only when begin returned true.
bool rv_editor_dialog_begin(const char *title, const rv_editor_theme &theme);
void rv_editor_dialog_end();

} // namespace rv_editor
