// Minimum tile size per pane kind, so a default layout's controls stay visible
// at the smallest supported window (1280x720 at UI scale 1).

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets_tile.hpp"

namespace rv_editor
{

namespace
{

// Minimum width in font sizes of a strip of controls.
constexpr int strip_min_width_em = 20;
// Minimum width in font sizes for log/output panes.
constexpr float log_pane_min_width_em = 30.0f;
// Minimum height in frame heights for log/output panes.
constexpr float log_pane_min_height_frames = 6.0f;

// A shelf of `rows` height as the tile lays it out: its padding on both sides and the spacing the
// tile's content ends with. The theme's pad is half the window padding the style gets from it.
float shelf_height(float rows)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    return style.WindowPadding.y + rows + style.ItemSpacing.y;
}

// The log panes' one control row, down to Source + Level + Find (at its own
// minimum) + More, once Follow, Wrap, Copy, Export and Clear View have all
// dropped behind More (rv_editor_output_controls); a few lines under it.
// Kinds not listed have no need of their own: the tree's generic per-tile
// minimum (rv_editor_workspace_draw) already covers them.
rv_editor_size rv_editor_shell_pane_minimum(rv_editor_pane_kind kind)
{
    const float fs = ImGui::GetFontSize();
    const float fh = ImGui::GetFrameHeight();
    const ImGuiStyle &style = ImGui::GetStyle();
    const float gap = style.ItemSpacing.y;
    const float large = rv_editor_tile_width({}, rv_editor_tile_size::large);
    const int32_t strip_w = static_cast<int32_t>(fs * strip_min_width_em);
    switch (kind) {
    // A strip is as tall as its tallest state at its minimum width, not as what it drew this frame. Controls: at
    // that width the buttons are small tiles and the transport row, the action slot, the mark row and the
    // message row with the profile button each take a row of their own.
    case rv_editor_pane_kind::controls:
        return { strip_w, static_cast<int32_t>(std::ceil(shelf_height(4.0f * fh + 3.0f * gap))) };
    case rv_editor_pane_kind::release_controls:
        return { strip_w, static_cast<int32_t>(std::ceil(shelf_height(large))) };
    // The scene tools' two rows, or the wrapped line of no scene when it is taller (the debug mark row is
    // a frame and a text line, less than the scene tools).
    case rv_editor_pane_kind::toolchest: {
        const float wrap = strip_w - 2.0f * style.WindowPadding.x - style.WindowPadding.y;
        const float no_scene = ImGui::CalcTextSize(rv_editor_text("pane_toolchest.no_scene"), nullptr, false, wrap).y;
        return { strip_w, static_cast<int32_t>(std::ceil(shelf_height(std::max(2.0f * fh + gap, no_scene)))) };
    }
    // The mode row's shelf, the well's padding around the picture and the status row over it.
    case rv_editor_pane_kind::game:
        return { 0,
            static_cast<int32_t>(std::ceil(shelf_height(fh) + style.WindowPadding.y + ImGui::GetTextLineHeightWithSpacing())) };
    case rv_editor_pane_kind::output:
    case rv_editor_pane_kind::runtime_log:
    case rv_editor_pane_kind::build_log:
        return { static_cast<int32_t>(fs * log_pane_min_width_em), static_cast<int32_t>(fh * log_pane_min_height_frames) };
    default:
        return { 0, 0 };
    }
}

} // namespace

void rv_editor_shell_set_minimum(rv_editor_shell &shell,
    rv_editor_pane_id pane,
    rv_editor_pane_kind kind,
    rv_editor_size /*game_need*/)
{
    const rv_editor_size need = rv_editor_shell_pane_minimum(kind);
    if (need.w > 0 || need.h > 0) {
        shell.ws.minimums[pane] = need;
    }
}

} // namespace rv_editor
