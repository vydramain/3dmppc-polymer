#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

// Game / Candidate Playtest status in two lines so neither clips: size and scale, then the state with its stale note.
struct rv_editor_game_status_lines {
    std::string size_scale;
    std::string state;
};

// size_scale is "WxH  scale"; state is the state text followed by stale (empty when the frame is live), joined by a space.
rv_editor_game_status_lines
rv_editor_game_status_compose(int width, int height, std::string_view scale, std::string_view state, std::string_view stale);

// The one-row text: "size_scale  state".
std::string rv_editor_game_status_joined(const rv_editor_game_status_lines &lines);

// Rows to draw: one "size_scale  state" row when joined_w (that text's pixel width) fits in width, otherwise
// size_scale and state as two rows (the state wraps by words when drawn).
std::vector<std::string> rv_editor_game_status_rows(const rv_editor_game_status_lines &lines, float width, float joined_w);

// Draws the rows wrapped in kind's colour; returns the screen y just under the last drawn line, where the
// picture and its dark background may start.
float rv_editor_game_status_draw(const std::vector<std::string> &rows, rv_editor_label_kind kind, const rv_editor_theme &theme);

// Whether the Game shelf draws its Run button: never in the release view, where Release Controls' Run runs the candidate.
bool rv_editor_game_shelf_shows_run(bool release_view);

} // namespace rv_editor
