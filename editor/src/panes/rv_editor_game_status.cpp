#include "panes/rv_editor_game_status.hpp"

#include <string>

#include "imgui.h"

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

std::string rv_editor_game_status_joined(const rv_editor_game_status_lines &lines)
{
    return lines.size_scale + "  " + lines.state;
}

std::vector<std::string> rv_editor_game_status_rows(const rv_editor_game_status_lines &lines, float width, float joined_w)
{
    if (width >= joined_w) {
        return { rv_editor_game_status_joined(lines) };
    }
    return { lines.size_scale, lines.state };
}

float rv_editor_game_status_draw(const std::vector<std::string> &rows, rv_editor_label_kind kind, const rv_editor_theme &theme)
{
    for (const std::string &row : rows) {
        rv_editor_label(row.c_str(), kind, theme, rv_editor_label_layout::wrap);
    }
    return ImGui::GetCursorScreenPos().y;
}

bool rv_editor_game_shelf_shows_run(bool release_view)
{
    return !release_view;
}

} // namespace rv_editor
