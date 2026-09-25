#include "ui/rv_editor_draw.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "theme/rv_editor_theme_imgui.hpp"

namespace rv_editor
{

namespace
{

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
    return ImVec2(std::floor((min.x + max.x - side) / 2.0f), std::floor((min.y + max.y - side) / 2.0f));
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
        for (float x = min.x + static_cast<float>(row % 2) * px; x + px <= max.x; x += 2.0f * px) {
            rv_editor_fill(dl, x, y, x + px, y + px, color);
        }
    }
}

void rv_editor_draw_diamond(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, bool selected)
{
    constexpr int radius = 5;
    const float px = static_cast<float>(theme.scale);
    const ImVec2 o = rv_editor_grid_origin(min, max, radius * 2 + 1, px);
    for (int dy = -radius; dy <= radius; ++dy) {
        const int half = radius - std::abs(dy);
        const float y = o.y + static_cast<float>(dy + radius) * px;
        const float left = o.x + static_cast<float>(radius - half) * px;
        const float right = o.x + static_cast<float>(radius + half + 1) * px;
        // Sunken: the upper-left edges are dark, the lower-right edges light.
        rv_editor_fill(dl, left, y, left + px, y + px, dy <= 0 ? theme.bevel_lo : theme.bevel_hi);
        rv_editor_fill(dl, right - px, y, right, y + px, dy < 0 ? theme.bevel_lo : theme.bevel_hi);
        if (half > 0) {
            const bool centre = selected && half > 2;
            const float inner = centre ? 2.0f * px : 0.0f;
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
    // 3 - i .. 3 + i and sits at depth 1 + i, or 4 - i when it points the other way.
    const float px = static_cast<float>(theme.scale);
    const ImVec2 o = rv_editor_grid_origin(min, max, 7, px);
    for (int i = 0; i < 4; ++i) {
        const float a = static_cast<float>(3 - i) * px;
        const float b = static_cast<float>(4 + i) * px;
        const bool forward = dir == ImGuiDir_Up || dir == ImGuiDir_Left;
        const float d = static_cast<float>(forward ? 1 + i : 4 - i) * px;
        if (dir == ImGuiDir_Up || dir == ImGuiDir_Down) {
            rv_editor_fill(dl, o.x + a, o.y + d, o.x + b, o.y + d + px, color);
        } else {
            rv_editor_fill(dl, o.x + d, o.y + a, o.x + d + px, o.y + b, color);
        }
    }
}

void rv_editor_draw_check(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, uint32_t color)
{
    static constexpr const char *rows[7] = {
        "......#",
        ".....##",
        "#...###",
        "##.###.",
        "#####..",
        ".###...",
        "..#....",
    };
    const float px = static_cast<float>(theme.scale);
    const ImVec2 o = rv_editor_grid_origin(min, max, 7, px);
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            if (rows[y][x] == '#') {
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
    for (float x = x0; x + px <= x1; x += 2.0f * px) {
        rv_editor_fill(dl, x, y0, x + px, y0 + px, theme.text);
        rv_editor_fill(dl, x, y1 - px, x + px, y1, theme.text);
    }
    for (float y = y0; y + px <= y1; y += 2.0f * px) {
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
    const uint32_t r = (color >> 16) & 0xff;
    const uint32_t g = (color >> 8) & 0xff;
    const uint32_t b = color & 0xff;
    const uint32_t ink = r * 299 + g * 587 + b * 114 > 128000 ? theme.dark : theme.text_bright;
    const char text[2] = { letter, '\0' };
    const ImVec2 size = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(std::floor((min.x + max.x - size.x) / 2.0f) + px, std::floor((min.y + max.y - size.y) / 2.0f) + px),
        rv_editor_col(ink), text);
}

void rv_editor_draw_glyph(ImDrawList *dl, ImVec2 min, ImVec2 max, const rv_editor_theme &theme, rv_editor_glyph glyph,
    uint32_t color)
{
    // One byte per row, top row first, bit 7 the leftmost column.
    static constexpr uint8_t pictures[][8] = {
        { 0x7e, 0x7f, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x18 }, // build
        { 0x40, 0x60, 0x70, 0x78, 0x78, 0x70, 0x60, 0x40 }, // run
        { 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00 }, // pause
        { 0x43, 0x63, 0x73, 0x7b, 0x7b, 0x73, 0x63, 0x43 }, // step
        { 0x00, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x00 }, // stop
        { 0x3d, 0x43, 0x87, 0x80, 0x80, 0x81, 0x42, 0x3c }, // reload
    };
    const uint8_t *rows = pictures[static_cast<int>(glyph)];
    const float px = std::max(1.0f, std::floor(std::min(max.x - min.x, max.y - min.y) / 8.0f));
    const ImVec2 at = rv_editor_grid_origin(min, max, 8, px);
    const float shadow = static_cast<float>(theme.scale);
    for (const float off : { shadow, 0.0f }) {
        const uint32_t ink = off != 0.0f ? theme.dark : color;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                if ((rows[y] & (0x80u >> x)) != 0) {
                    rv_editor_fill(dl, at.x + x * px + off, at.y + y * px + off, at.x + (x + 1) * px + off,
                        at.y + (y + 1) * px + off, ink);
                }
            }
        }
    }
}

} // namespace rv_editor
