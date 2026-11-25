#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"
#include "layout/rv_editor_tile.hpp"
#include "panes/rv_editor_game_fit.hpp"
#include "ui/rv_editor_widgets_text.hpp"

namespace rv_editor
{

// The one minimum of the Game tile, whatever its state. Call inside a frame or with the UI font current.
rv_editor_size rv_editor_game_minimum();

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

// A screen rectangle.
struct rv_editor_game_rect {
    ImVec2 min, max;
};

// Where the frame goes: from the whole picture area, the status text takes no height from it.
rv_editor_game_view
rv_editor_game_picture_view(int frame_w, int frame_h, float area_w, float area_h, rv_editor_game_scale mode);

// The least picture width at which the longest status (size and scale, state, stale note) wraps into two lines.
float rv_editor_game_picture_min_width();

// The click area of the picture: the whole picture area from origin, text included.
rv_editor_game_rect rv_editor_game_click_rect(ImVec2 origin, ImVec2 area);

} // namespace rv_editor
