#pragma once

#include "theme/rv_editor_theme.hpp"

namespace rv_editor
{

// An Open Inventor thumbwheel: a ridged wheel dragged along its length
// turns a value without end. It has a label, takes the keyboard (arrows turn it
// by steps), and a double click asks for its home value. Returns the turn this
// frame in pixels; `reset` becomes true on the double click.
float rv_editor_thumbwheel(const char *id,
    const char *label,
    bool vertical,
    float length,
    const rv_editor_theme &theme,
    bool &reset);

} // namespace rv_editor
