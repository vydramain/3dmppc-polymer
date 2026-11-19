#include "panes/rv_editor_game_status.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#include "imgui.h"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets_picture.hpp"

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

rv_editor_game_view rv_editor_game_picture_view(int frame_w, int frame_h, float area_w, float area_h, rv_editor_game_scale mode)
{
    return rv_editor_game_place(frame_w, frame_h, area_w, area_h, mode);
}

float rv_editor_game_picture_min_width()
{
    // The minimums are asked every frame; the answer changes only with the font or its size.
    static const ImFont *cached_font = nullptr;
    static float cached_size = 0.0f;
    static float cached_width = 0.0f;
    const ImFont *font = ImGui::GetIO().Fonts->Fonts[0];
    if (font == cached_font && font->LegacySize == cached_size) {
        return cached_width;
    }
    const uint32_t longest_number = 999999; // six digits: the widest build or candidate number
    const std::string builds = rv_editor_text_format("pane_game.stopped_builds", std::make_format_args(longest_number));
    const std::string candidate = rv_editor_text_format("pane_game.candidate_image", std::make_format_args(longest_number));
    const std::string states[] = { rv_editor_text("pane_game.stopped_rebuilds"),
        builds,
        rv_editor_text("pane_game.no_candidate"),
        candidate,
        rv_editor_text("pane_game.frame_waiting"),
        rv_editor_text("pane_game.console_disconnected"),
        rv_editor_text("pane_game.not_stopping"),
        rv_editor_text("pane_game.timeout"),
        rv_editor_text("pane_game.playing_keyboard"),
        rv_editor_text("pane_game.click_to_play") };
    const float scale = 0.5f;
    const char *mode = "3x";
    const uint32_t number = 999999;
    const uint64_t frame = 99999999;
    const char *state_name = "disconnected";
    const std::string stale = rv_editor_text_format("pane_game.stale_session", std::make_format_args(number, state_name));
    const std::string scales[] = { rv_editor_text_format("pane_game.scale_fit_text", std::make_format_args(scale)),
        rv_editor_text_format("pane_game.scale_integer_reduced", std::make_format_args(scale)),
        rv_editor_text_format("pane_game.scale_other_reduced", std::make_format_args(mode, scale)) };
    std::vector<std::string> texts;
    for (const std::string &sc : scales) {
        for (const std::string &state : states) {
            const std::string framed = rv_editor_text_format("pane_game.frame_status", std::make_format_args(frame, state));
            for (const std::string &s : { state, framed }) {
                texts.push_back(rv_editor_game_status_joined(rv_editor_game_status_compose(320, 240, sc, s, "")));
                texts.push_back(rv_editor_game_status_joined(rv_editor_game_status_compose(320, 240, sc, s, stale)));
            }
        }
    }
    cached_font = font;
    cached_size = font->LegacySize;
    cached_width = rv_editor_picture_min_width(texts);
    return cached_width;
}

rv_editor_size rv_editor_game_minimum()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    const float fh = ImGui::GetFrameHeight();
    const float pad = style.ItemInnerSpacing.y;
    // The mode row's shelf, the well's padding and the row in it: the status line, or the no-project row when taller.
    const float height =
        style.WindowPadding.y + 2.0f * pad + fh + style.ItemSpacing.y + 2.0f * pad + std::max(fs + style.ItemSpacing.y, fh);
    return { static_cast<int32_t>(std::ceil(rv_editor_game_picture_min_width() + 2.0f * style.WindowPadding.x)),
        static_cast<int32_t>(std::ceil(height)) };
}

rv_editor_game_rect rv_editor_game_click_rect(ImVec2 origin, ImVec2 area)
{
    return { origin, ImVec2(origin.x + area.x, origin.y + area.y) };
}

} // namespace rv_editor
