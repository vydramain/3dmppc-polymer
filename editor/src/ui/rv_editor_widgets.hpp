#pragma once

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

} // namespace rv_editor
