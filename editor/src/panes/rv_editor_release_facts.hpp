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

// Large tiles when the five with their gaps, the facts text of `facts_width` and the row
// fit `avail`, else medium, else small; the same choice as the transport bar.
rv_editor_tile_size rv_editor_release_tile_size(const rv_editor_tile (&tiles)[5], float facts_width, float avail);

// The same by the width alone: the room the facts text takes is a constant, not the state's text.
rv_editor_tile_size rv_editor_release_tile_size(const rv_editor_tile (&tiles)[5], float avail);

} // namespace rv_editor
