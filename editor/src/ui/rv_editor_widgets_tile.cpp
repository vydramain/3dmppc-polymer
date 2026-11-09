// Tiles: a transport square, a code beside its label, and a letter-sized square.

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string>

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widget_item.hpp"
#include "ui/rv_editor_widgets_tile.hpp"

namespace rv_editor
{

namespace
{

// Transport button reference label: measured width for square sizing.
constexpr const char *tool_button_ref_label = "MMMMMM";
// Transport button minimum height: line spacing multiplier.
constexpr float tool_button_line_mult = 3.0f;
// Medium tile padding multiplier: left, gap and right of the code and the label.
constexpr float tile_medium_pad_mult = 3.0f;
// Code font scale maximum: primary try for tool button code.
constexpr float code_font_scale_max = 2.0f;
// Code font scale medium: first fallback when code does not fit.
constexpr float code_font_scale_mid = 1.5f;
// Centring divisor: divide by 2 to find the midpoint.
constexpr float center_div = 2.0f;
// Both-sides multiplier: left and right frame padding.
constexpr float both_sides_mult = 2.0f;

// A tile's tooltip: its name (or the label) and the shortcut in brackets.
std::string rv_editor_tile_tip(const rv_editor_tile &tile)
{
    const std::string name =
        tile.name != nullptr ? std::string(tile.name) : std::string(tile.label, rv_editor_label_end(tile.label));
    return tile.shortcut != nullptr ? name + " (" + tile.shortcut + ")" : name;
}

// A tile's item with its tooltip, unless disabled.
rv_editor_item rv_editor_tile_add(const rv_editor_tile &tile, ImVec2 size, const rv_editor_state &state)
{
    const rv_editor_item item = rv_editor_item_add(tile.label, size, state);
    if (!item.disabled) {
        const std::string tip = rv_editor_tile_tip(tile);
        ImGui::SetItemTooltip("%s", tip.c_str());
    }
    return item;
}

// One square for every transport button: a six-letter label with padding, or
// the letter over a line of text, whichever is larger.
float rv_editor_tile_large_width(const rv_editor_tile &)
{
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const float line = ImGui::GetTextLineHeight();
    const float ref_width = ImGui::CalcTextSize(tool_button_ref_label).x + pad.x * both_sides_mult;
    const float line_height = line * tool_button_line_mult + pad.y * tool_button_line_mult;
    return std::floor(std::max(ref_width, line_height));
}

bool rv_editor_tile_large_draw(const rv_editor_tile &tile, const rv_editor_theme &theme, const rv_editor_state &state)
{
    const float side = rv_editor_tile_large_width(tile);
    const rv_editor_item item = rv_editor_tile_add(tile, ImVec2(side, side), state);
    const char *end = rv_editor_label_end(tile.label);
    const bool down = item.held && item.hovered;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_button_face(dl, item, theme, down, tile.selected);
    const float nudge = down ? static_cast<float>(theme.scale) : 0.0f;
    const float pad = ImGui::GetStyle().FramePadding.y;
    // The code at the largest of 2x/1.5x/1x the font's size that still fits the
    // square's width, over the label, both centred across the square.
    const float fit = side - pad * both_sides_mult;
    float big = ImGui::GetFontSize() * code_font_scale_max;
    float code_w = ImGui::GetFont()->CalcTextSizeA(big, FLT_MAX, 0.0f, tile.code).x;
    for (const float scale : { code_font_scale_mid, 1.0f }) {
        if (code_w <= fit) {
            break;
        }
        big = ImGui::GetFontSize() * scale;
        code_w = ImGui::GetFont()->CalcTextSizeA(big, FLT_MAX, 0.0f, tile.code).x;
    }
    const float y = std::floor((item.min.y + item.max.y - big - ImGui::GetTextLineHeight() - pad) / center_div + nudge);
    // Dimmed: halfway to the window colour.
    const uint32_t dim = item.disabled ? rv_editor_blend_color(tile.color, theme.window) : tile.color;
    dl->AddText(ImGui::GetFont(),
        big,
        ImVec2(std::floor((item.min.x + item.max.x - code_w) / center_div + nudge), y),
        rv_editor_col(dim),
        tile.code);
    const ImVec2 size = ImGui::CalcTextSize(tile.label, end);
    dl->AddText(ImVec2(std::floor((item.min.x + item.max.x - size.x) / center_div + nudge), y + big + pad),
        rv_editor_col(rv_editor_item_text(theme, item)),
        tile.label,
        end);
    if (item.focused) {
        rv_editor_draw_focus(dl, item.min, item.max, theme);
    }
    return item.clicked;
}

// The code and the label in a row: padding, code, gap, label, padding.
float rv_editor_tile_medium_width(const rv_editor_tile &tile)
{
    const float pad = ImGui::GetStyle().FramePadding.x;
    const float code_w = ImGui::CalcTextSize(tile.code).x;
    const float label_w = ImGui::CalcTextSize(tile.label, rv_editor_label_end(tile.label)).x;
    return pad * tile_medium_pad_mult + code_w + label_w;
}

bool rv_editor_tile_medium_draw(const rv_editor_tile &tile, const rv_editor_theme &theme, const rv_editor_state &state)
{
    const float width = rv_editor_tile_medium_width(tile);
    const rv_editor_item item = rv_editor_tile_add(tile, ImVec2(width, ImGui::GetFrameHeight()), state);
    const char *end = rv_editor_label_end(tile.label);
    const bool down = item.held && item.hovered;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_button_face(dl, item, theme, down, tile.selected);
    const float nudge = down ? static_cast<float>(theme.scale) : 0.0f;
    const float pad = ImGui::GetStyle().FramePadding.x;
    const float code_w = ImGui::CalcTextSize(tile.code).x;
    const float y = std::floor((item.min.y + item.max.y - ImGui::GetTextLineHeight()) / center_div + nudge);
    const float x = std::floor(item.min.x + pad + nudge);
    // Dimmed like a disabled picture: halfway to the window colour.
    const uint32_t ink = item.disabled ? rv_editor_blend_color(tile.color, theme.window) : tile.color;
    dl->AddText(ImVec2(x, y), rv_editor_col(ink), tile.code);
    dl->AddText(ImVec2(std::floor(x + code_w + pad), y), rv_editor_col(rv_editor_item_text(theme, item)), tile.label, end);
    if (item.focused) {
        rv_editor_draw_focus(dl, item.min, item.max, theme);
    }
    return item.clicked;
}

// A square of frame height, as a letter button.
float rv_editor_tile_small_width(const rv_editor_tile &)
{
    return ImGui::GetFrameHeight();
}

bool rv_editor_tile_small_draw(const rv_editor_tile &tile, const rv_editor_theme &theme, const rv_editor_state &state)
{
    const std::string tip = rv_editor_tile_tip(tile);
    const rv_editor_item item = rv_editor_letter_add(tile.label, tip.c_str(), state);
    const bool down = item.held && item.hovered;
    rv_editor_letter_draw(item, tile.code, tile.color, theme, down, tile.selected);
    return item.clicked;
}

using tile_width_fn = float (*)(const rv_editor_tile &);
using tile_draw_fn = bool (*)(const rv_editor_tile &, const rv_editor_theme &, const rv_editor_state &);

// One entry per rv_editor_tile_size, in enum order: how the size is measured and drawn.
struct rv_editor_tile_kind {
    tile_width_fn width;
    tile_draw_fn draw;
};

constexpr rv_editor_tile_kind tile_kinds[] = {
    { rv_editor_tile_large_width, rv_editor_tile_large_draw },
    { rv_editor_tile_medium_width, rv_editor_tile_medium_draw },
    { rv_editor_tile_small_width, rv_editor_tile_small_draw },
};

// A new rv_editor_tile_size without a table entry would index past the end.
static_assert(std::size(tile_kinds) == static_cast<size_t>(rv_editor_tile_size::small) + 1,
    "tile_kinds needs one entry per rv_editor_tile_size");

} // namespace

float rv_editor_tile_width(const rv_editor_tile &tile, rv_editor_tile_size size)
{
    return tile_kinds[static_cast<size_t>(size)].width(tile);
}

bool rv_editor_tile_button(const rv_editor_tile &tile,
    rv_editor_tile_size size,
    const rv_editor_theme &theme,
    const rv_editor_state &state)
{
    return tile_kinds[static_cast<size_t>(size)].draw(tile, theme, state);
}

} // namespace rv_editor
