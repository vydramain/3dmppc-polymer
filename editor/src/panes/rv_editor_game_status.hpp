#pragma once

#include <string>
#include <string_view>

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

// Whether the Game shelf draws its Run button: never in the release view, where Release Controls' Run runs the candidate.
bool rv_editor_game_shelf_shows_run(bool release_view);

} // namespace rv_editor
