// Observe's facts, in a file of their own.

#pragma once

#include "app/rv_editor_app.hpp"
#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// The rows of the facts list: the session and what the console said of itself. Call inside rv_editor_tiled_area_begin with
// facts set.
void rv_editor_observe_facts(const rv_editor_session &s, const rv_editor_theme &theme);

} // namespace rv_editor
