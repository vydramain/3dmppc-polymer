#include "panes/rv_editor_game_status.hpp"

#include <string>

namespace rv_editor
{

rv_editor_game_status_lines
rv_editor_game_status_compose(int width, int height, std::string_view scale, std::string_view state, std::string_view stale)
{
    rv_editor_game_status_lines lines;
    lines.size_scale = std::to_string(width) + "x" + std::to_string(height) + "  " + std::string(scale);
    lines.state = std::string(state);
    if (!stale.empty()) {
        lines.state += state.empty() ? "" : " ";
        lines.state += stale;
    }
    return lines;
}

bool rv_editor_game_shelf_shows_run(bool release_view)
{
    return !release_view;
}

} // namespace rv_editor
