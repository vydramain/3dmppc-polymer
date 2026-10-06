#include "ui/rv_editor_draw.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

namespace
{

// Halves a dimension to center a grid or text.
constexpr float center_divisor = 2.0f;
// Period of checkerboard pattern for stipple and focus (alternating, 2x2 step).
constexpr int checkerboard_pattern_period = 2;
constexpr float pattern_step_multiplier = static_cast<float>(checkerboard_pattern_period);
// Grid size for arrow and check icons (7x7 cells).
constexpr int arrow_check_grid_size = 7;
// Number of rows in arrow triangle (0 to 3 iterations).
constexpr int arrow_depth_rows = 4;
// Tip column at grid centre (spans 3-i to 4+i).
constexpr int arrow_tip_col = 3;
// Diamond centre fill threshold (radius 5, show when half > 2).
constexpr int diamond_centre_threshold = 2;
// Multiplier for diamond diameter from radius (2*radius + 1).
constexpr int diamond_diameter_multiplier = 2;
// Character marking a filled cell in check pattern.
constexpr char check_filled_cell = '#';
// ITU-R BT.601 luma red coefficient (scaled by 1000).
constexpr uint32_t luma_r_coeff = 299;
// ITU-R BT.601 luma green coefficient (scaled by 1000).
constexpr uint32_t luma_g_coeff = 587;
// ITU-R BT.601 luma blue coefficient (scaled by 1000).
constexpr uint32_t luma_b_coeff = 114;
// Luma threshold for bright ink on coloured backgrounds (ITU-R BT.601 * 1000).
constexpr uint32_t luma_brightness_threshold = 128000;

// Whole-pixel rectangle; every primitive below ends here.
void rv_editor_fill(ImDrawList *dl, float x0, float y0, float x1, float y1, uint32_t color)
{
    dl->AddRectFilled(ImVec2(std::floor(x0), std::floor(y0)), ImVec2(std::floor(x1), std::floor(y1)),
        rv_editor_col(color));
}

// Top-left corner of a `cells` x `cells` grid of scaled pixels centred in [min, max).
ImVec2 rv_editor_grid_origin(ImVec2 min, ImVec2 max, int cells, float px)
{
    const float side = static_cast<float>(cells) * px;
    return ImVec2(std::floor((min.x + max.x - side) / center_divisor),
        std::floor((min.y + max.y - side) / center_divisor));
}

} // namespace

void rv_editor_draw_bevel(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, rv_editor_bevel kind)
{
    const float w = static_cast<float>(theme.bevel_px * theme.scale);
    const bool raised = kind == rv_editor_bevel::raised;
    const uint32_t light = raised ? theme.bevel_hi : theme.bevel_lo;
    const uint32_t dark = raised ? theme.bevel_lo : theme.bevel_hi;
    rv_editor_fill(dl, min.x, min.y, max.x - w, min.y + w, light);
    rv_editor_fill(dl, min.x, min.y + w, min.x + w, max.y - w, light);
    rv_editor_fill(dl, min.x, max.y - w, max.x, max.y, dark);
    rv_editor_fill(dl, max.x - w, min.y, max.x, max.y - w, dark);
}

void rv_editor_draw_frame(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, uint32_t color)
{
    const float w = static_cast<float>(theme.bevel_px * theme.scale);
    rv_editor_fill(dl, min.x, min.y, max.x, min.y + w, color);
    rv_editor_fill(dl, min.x, max.y - w, max.x, max.y, color);
    rv_editor_fill(dl, min.x, min.y + w, min.x + w, max.y - w, color);
    rv_editor_fill(dl, max.x - w, min.y + w, max.x, max.y - w, color);
}

void rv_editor_draw_panel(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, uint32_t fill,
    rv_editor_bevel kind)
{
    rv_editor_fill(dl, min.x, min.y, max.x, max.y, fill);
    rv_editor_draw_bevel(dl, min, max, theme, kind);
}

void rv_editor_draw_stipple(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, uint32_t color)
{
    const float px = static_cast<float>(theme.scale);
    int row = 0;
    for (float y = min.y; y + px <= max.y; y += px, ++row) {
        for (float x = min.x + static_cast<float>(row % checkerboard_pattern_period) * px; x + px <= max.x;
            x += pattern_step_multiplier * px) {
            rv_editor_fill(dl, x, y, x + px, y + px, color);
        }
    }
}

void rv_editor_draw_diamond(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, bool selected)
{
    constexpr int radius = 5;
    const float px = static_cast<float>(theme.scale);
    const int diameter = radius * diamond_diameter_multiplier + 1;
    const ImVec2 o = rv_editor_grid_origin(min, max, diameter, px);
    for (int dy = -radius; dy <= radius; ++dy) {
        const int half = radius - std::abs(dy);
        const float y = o.y + static_cast<float>(dy + radius) * px;
        const float left = o.x + static_cast<float>(radius - half) * px;
        const float right = o.x + static_cast<float>(radius + half + 1) * px;
        // Sunken: the upper-left edges are dark, the lower-right edges light.
        rv_editor_fill(dl, left, y, left + px, y + px, dy <= 0 ? theme.bevel_lo : theme.bevel_hi);
        rv_editor_fill(dl, right - px, y, right, y + px, dy < 0 ? theme.bevel_lo : theme.bevel_hi);
        if (half > 0) {
            const bool centre = selected && half > diamond_centre_threshold;
            const float inner = centre ? pattern_step_multiplier * px : 0.0f;
            rv_editor_fill(dl, left + px, y, right - px, y + px, theme.inset);
            if (centre) {
                rv_editor_fill(dl, left + inner, y, right - inner, y + px, theme.selection);
            }
        }
    }
}

void rv_editor_draw_arrow(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, ImGuiDir dir,
    uint32_t color)
{
    // A 7-wide, 4-deep triangle in a 7x7 grid: row i (tip = 0) spans cells
    // tip_col - i .. tip_col + i and sits at depth 1 + i, or 4 - i when it points the other way.
    const float px = static_cast<float>(theme.scale);
    const ImVec2 o = rv_editor_grid_origin(min, max, arrow_check_grid_size, px);
    for (int i = 0; i < arrow_depth_rows; ++i) {
        const float a = static_cast<float>(arrow_tip_col - i) * px;
        const float b = static_cast<float>(arrow_tip_col + 1 + i) * px;
        const bool forward = dir == ImGuiDir_Up || dir == ImGuiDir_Left;
        const float d = static_cast<float>(forward ? 1 + i : arrow_depth_rows - i) * px;
        if (dir == ImGuiDir_Up || dir == ImGuiDir_Down) {
            rv_editor_fill(dl, o.x + a, o.y + d, o.x + b, o.y + d + px, color);
        } else {
            rv_editor_fill(dl, o.x + d, o.y + a, o.x + d + px, o.y + b, color);
        }
    }
}

void rv_editor_draw_check(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, uint32_t color)
{
    static constexpr const char *rows[arrow_check_grid_size] = {
        "......#",
        ".....##",
        "#...###",
        "##.###.",
        "#####..",
        ".###...",
        "..#....",
    };
    const float px = static_cast<float>(theme.scale);
    const ImVec2 o = rv_editor_grid_origin(min, max, arrow_check_grid_size, px);
    for (int y = 0; y < arrow_check_grid_size; ++y) {
        for (int x = 0; x < arrow_check_grid_size; ++x) {
            if (rows[y][x] == check_filled_cell) {
                const float fx = o.x + static_cast<float>(x) * px;
                const float fy = o.y + static_cast<float>(y) * px;
                rv_editor_fill(dl, fx, fy, fx + px, fy + px, color);
            }
        }
    }
}

void rv_editor_draw_focus(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme)
{
    const float px = static_cast<float>(theme.scale);
    const float inset = static_cast<float>((theme.bevel_px + 1) * theme.scale);
    const float x0 = min.x + inset;
    const float y0 = min.y + inset;
    const float x1 = max.x - inset;
    const float y1 = max.y - inset;
    for (float x = x0; x + px <= x1; x += pattern_step_multiplier * px) {
        rv_editor_fill(dl, x, y0, x + px, y0 + px, theme.text);
        rv_editor_fill(dl, x, y1 - px, x + px, y1, theme.text);
    }
    for (float y = y0; y + px <= y1; y += pattern_step_multiplier * px) {
        rv_editor_fill(dl, x0, y, x0 + px, y + px, theme.text);
        rv_editor_fill(dl, x1 - px, y, x1, y + px, theme.text);
    }
}

void rv_editor_draw_chip(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, char letter,
    uint32_t color)
{
    rv_editor_fill(dl, min.x, min.y, max.x, max.y, theme.dark);
    const float px = static_cast<float>(theme.scale);
    rv_editor_fill(dl, min.x + px, min.y + px, max.x - px, max.y - px, color);
    // Perceived brightness decides the ink.
    const uint32_t r = (color >> channel_shift_red) & rgb_channel_mask;
    const uint32_t g = (color >> channel_shift_green) & rgb_channel_mask;
    const uint32_t b = color & rgb_channel_mask;
    const uint32_t luma = r * luma_r_coeff + g * luma_g_coeff + b * luma_b_coeff;
    const uint32_t ink = luma > luma_brightness_threshold ? theme.dark : theme.text_bright;
    const char text[] = { letter, '\0' };
    const ImVec2 size = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(std::floor((min.x + max.x - size.x) / center_divisor) + px,
                    std::floor((min.y + max.y - size.y) / center_divisor) + px),
        rv_editor_col(ink), text);
}

} // namespace rv_editor
