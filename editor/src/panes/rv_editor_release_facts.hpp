#pragma once

#include <string>

#include "release/rv_editor_candidate.hpp"
#include "ui/rv_editor_widgets_tile.hpp"

namespace rv_editor
{

// What the build in progress is, if any: it prefixes the first line.
enum class rv_editor_release_building {
    none,
    candidate,
    development,
};

// Row one's text beside the tiles. `c` is the shown candidate (nullptr: none yet).
// `second_line` is false when there is no candidate: the strip then has one line only.
struct rv_editor_release_facts {
    std::string first_line;
    bool second_line = true;
};

// "Candidate #N | Ready|Not ready | K/M checks"; no sha256, no hashing note. A build in
// progress puts its own prefix before it.
rv_editor_release_facts rv_editor_release_facts_make(const rv_editor_candidate *c, rv_editor_release_building building);

// The hover text of the candidate number: the full sha256.
std::string rv_editor_release_number_tooltip(const rv_editor_candidate &c);

// The strip's measures, taken with the UI font current. The five squares, the width the column right of them needs (the
// action slot, or the longest facts a state shows, whichever is wider), the least width of the tile's body and its height at
// a body `width` wide: the same in every state, and higher from 640 px, where the squares grow.
inline constexpr int release_buttons = 5;
float rv_editor_release_column_min_width();
float rv_editor_release_min_width();
float rv_editor_release_height(float width);

} // namespace rv_editor
