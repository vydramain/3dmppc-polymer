#pragma once

#include <cstdint>

#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widget_item.hpp"

namespace rv_editor
{

// Sizes of a tile: large is the transport square, medium a row of frame height
// with the code beside the label, small a square of frame height with the code.
enum class rv_editor_tile_size {
    large,
    medium,
    small,
};

// What a tile shows: `code` in `color` over or beside `label`. The tooltip is
// `name` (nullptr: the label) and `shortcut` in brackets (nullptr: none).
// `selected` paints the surface in the selection colour. `label` and `code` must not be
// nullptr to draw; the width of a large tile needs neither.
struct rv_editor_tile {
    const char *label = nullptr;
    const char *code = nullptr;
    uint32_t color = 0;
    const char *name = nullptr;
    const char *shortcut = nullptr;
    bool selected = false;
};

// A tile of `size`, drawn in the current window; a disabled tile dims the code and
// says why. Returns true when clicked.
bool rv_editor_tile_button(const rv_editor_tile &tile,
    rv_editor_tile_size size,
    const rv_editor_theme &theme,
    const rv_editor_state &state = {});

// Width a tile of `size` takes. Large is a square; medium and small are frame height tall.
float rv_editor_tile_width(const rv_editor_tile &tile, rv_editor_tile_size size);

} // namespace rv_editor
