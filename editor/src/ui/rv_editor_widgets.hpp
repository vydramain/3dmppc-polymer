#pragma once

#include <cstddef>

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

} // namespace rv_editor
