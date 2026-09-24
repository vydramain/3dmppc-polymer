#pragma once

#include <cstddef>

#include "layout/rv_editor_tile.hpp"
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
// A button that stays pressed while *on.
bool rv_editor_toggle(const char *label, bool *on, const rv_editor_theme &theme, const rv_editor_state &state = {});
bool rv_editor_checkbox(const char *label, bool *on, const rv_editor_theme &theme, const rv_editor_state &state = {});
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

// --- panes --------------------------------------------------------------------
// Trees, lists, tables and tab strips are ImGui's own (TreeNodeEx, BeginListBox,
// BeginTable, BeginTabBar) in the theme's colours; these are the pane pieces
// ImGui has no public form of.

// Draggable bar between two panes. rv_editor_axis::x: a vertical bar `length`
// tall that moves along X. Keeps *a >= min_a and *b >= min_b; returns true on change.
bool rv_editor_splitter(const char *id, rv_editor_axis axis, float length, float *a, float *b, float min_a, float min_b,
    const rv_editor_theme &theme, const rv_editor_state &state = {});

// Full-width pane title bar: stippled when active, dimmed when not.
void rv_editor_pane_header(const char *title, bool active, const rv_editor_theme &theme);

// --- the tiled workspace ------------------------------------------------------

// What the window shows: the pane registry and the tile tree over it
// (docs/adr/0002-tiling.md). Views only; no model lives here.
struct rv_editor_workspace
{
    rv_editor_pane_registry panes;
    rv_editor_layout layout;
    uint32_t focused_leaf = rv_editor_tile_none;
};

// Draws one pane's content into the current ImGui window.
using rv_editor_pane_draw_fn = void (*)(rv_editor_pane_id pane, rv_editor_pane_kind kind, const rv_editor_theme &theme);

// Title of a pane kind as the tab and header show it.
const char *rv_editor_pane_title(rv_editor_pane_kind kind);

// Fills the main viewport with the workspace. A splitter drag changes its
// split's ratio.
void rv_editor_workspace_draw(rv_editor_workspace &ws, const rv_editor_theme &theme, rv_editor_pane_draw_fn draw_pane);

// --- status, log, transport, dialogs --------------------------------------------
// Menus and context menus are ImGui's own (BeginMenuBar, BeginMenu, MenuItem,
// BeginPopupContextItem) in the theme's colours.

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

// Lamp and label; each kind has its own symbol as well as its own colour.
void rv_editor_status(const char *label, rv_editor_status_kind kind, const rv_editor_theme &theme);

// One line of process output: time, source, severity tag, text.
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
