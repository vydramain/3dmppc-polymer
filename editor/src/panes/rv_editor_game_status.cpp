#include "panes/rv_editor_game_status.hpp"

#include <string>

#include "imgui.h"
#include "theme/rv_editor_theme_imgui.hpp"

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

rv_editor_game_overlay rv_editor_game_status_overlay(ImDrawList *dl,
    const std::vector<std::string> &rows,
    rv_editor_label_kind kind,
    const rv_editor_theme &theme,
    ImVec2 origin,
    float width)
{
    constexpr uint32_t backing_alpha = 0xb0; // the picture shows through, the text stays readable
    rv_editor_game_overlay out;
    std::vector<float> heights;
    float total = 0.0f;
    for (const std::string &row : rows) {
        heights.push_back(ImGui::CalcTextSize(row.c_str(), nullptr, false, width).y);
        total += heights.back();
    }
    out.backing = { origin, ImVec2(origin.x + width, origin.y + total) };
    dl->AddRectFilled(out.backing.min,
        out.backing.max,
        (rv_editor_col(theme.code_base) & ~IM_COL32_A_MASK) | (backing_alpha << IM_COL32_A_SHIFT));
    float y = origin.y;
    for (size_t i = 0; i < rows.size(); i++) {
        out.rows.push_back({ ImVec2(origin.x, y), ImVec2(origin.x + width, y + heights[i]) });
        dl->AddText(nullptr,
            0.0f,
            ImVec2(origin.x, y),
            rv_editor_col(rv_editor_label_colour(kind, theme)),
            rows[i].c_str(),
            nullptr,
            width);
        y += heights[i];
    }
    return out;
}

rv_editor_game_view rv_editor_game_picture_view(int frame_w, int frame_h, float area_w, float area_h, rv_editor_game_scale mode)
{
    return rv_editor_game_place(frame_w, frame_h, area_w, area_h, mode);
}

rv_editor_game_rect rv_editor_game_click_rect(ImVec2 origin, ImVec2 area)
{
    return { origin, ImVec2(origin.x + area.x, origin.y + area.y) };
}

} // namespace rv_editor
