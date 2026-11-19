#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"
#include "panes/rv_editor_game_fit.hpp"
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

// A screen rectangle.
struct rv_editor_game_rect {
    ImVec2 min, max;
};

// Where the status rows landed: the translucent backing and one rectangle per input row (a wrapped row is as tall as
// all its lines). Rows follow each other from the backing's top; the backing is exactly as tall as all of them.
struct rv_editor_game_overlay {
    rv_editor_game_rect backing;
    std::vector<rv_editor_game_rect> rows;
};

// Draws into dl, over whatever dl already holds: first the backing (a theme colour with alpha below 1) from origin,
// width wide, then the rows in kind's colour wrapped at width. Draws nothing else and moves no ImGui cursor.
rv_editor_game_overlay rv_editor_game_status_overlay(ImDrawList *dl,
    const std::vector<std::string> &rows,
    rv_editor_label_kind kind,
    const rv_editor_theme &theme,
    ImVec2 origin,
    float width);

// Where the frame goes: from the whole picture area, the status text takes no height from it.
rv_editor_game_view
rv_editor_game_picture_view(int frame_w, int frame_h, float area_w, float area_h, rv_editor_game_scale mode);

// The least picture width at which the longest status (size and scale, state, stale note) wraps into two lines.
float rv_editor_game_picture_min_width();

// The click area of the picture: the whole picture area from origin, text included.
rv_editor_game_rect rv_editor_game_click_rect(ImVec2 origin, ImVec2 area);

} // namespace rv_editor
