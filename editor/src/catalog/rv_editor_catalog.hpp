#pragma once

#include "imgui.h"

#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// Widget Catalog (UI-07): every component in every state, drawn with the real
// theme, for checking the theme by eye. It fills the window.
void rv_editor_catalog_draw(const rv_editor_theme &theme);

// One section per component group, each in its own file.
void rv_editor_catalog_theme(const rv_editor_theme &theme);

// Reserves a `size` item in the layout and returns its top-left corner.
ImVec2 rv_editor_catalog_reserve(ImVec2 size);

} // namespace rv_editor
