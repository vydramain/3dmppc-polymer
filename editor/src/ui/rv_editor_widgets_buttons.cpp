// Buttons, icon buttons, toggles, check boxes and diamond radios.

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Button horizontal padding multiplier: left and right of text.
constexpr float button_pad_mult = 4.0f;
// Transport button reference label: measured width for square sizing.
constexpr const char *tool_button_ref_label = "MMMMMM";
// Transport button minimum height: line spacing multiplier.
constexpr float tool_button_line_mult = 3.0f;
// Code font scale maximum: primary try for tool button code.
constexpr float code_font_scale_max = 2.0f;
// Code font scale medium: first fallback when code does not fit.
constexpr float code_font_scale_mid = 1.5f;
// Letter button inset: scale factor for 2-pixel border clearance.
constexpr float letter_button_inset_mult = 2.0f;
// Icon button padding multiplier: space around scaled icon.
constexpr float icon_button_pad_mult = 2.0f;
// Disabled icon button alpha: tint opacity at 96 out of 255.
constexpr int icon_disabled_alpha = 96;
// Command button padding multiplier: left and right of contents.
constexpr float command_button_pad_mult = 3.0f;
// Focus frame offset: pixels beyond widget border (scale units).
constexpr int focus_frame_offset = 3;
// Centring divisor: divide by 2 to find the midpoint.
constexpr float center_div = 2.0f;
// Both-sides multiplier: left and right frame padding.
constexpr float both_sides_mult = 2.0f;
// Full colour channel: max intensity.
constexpr int ch_full = 255;

ImVec2 rv_editor_floor(ImVec2 v)
{
    return ImVec2(std::floor(v.x), std::floor(v.y));
}

// Blend two 0xRRGGBB colours by averaging each channel.
uint32_t rv_editor_blend_color(uint32_t color1, uint32_t color2)
{
    // Two colours are averaged.
    constexpr uint32_t blended_colors = 2;

    auto channel_avg = [](uint32_t c1, uint32_t c2, int shift) {
        return (((c1 >> shift) & rgb_channel_mask) + ((c2 >> shift) & rgb_channel_mask)) / blended_colors;
    };
    const uint32_t r = channel_avg(color1, color2, channel_shift_red);
    const uint32_t g = channel_avg(color1, color2, channel_shift_green);
    const uint32_t b = channel_avg(color1, color2, 0);

    return (r << channel_shift_red) | (g << channel_shift_green) | b;
}

// Label centred in [min, max), nudged one scaled pixel down-right when pressed.
void rv_editor_label_centred(ImDrawList *dl, const char *label, ImVec2 min, ImVec2 max, const rv_editor_theme &t,
    const rv_editor_item &item, bool pressed)
{
    const char *end = rv_editor_label_end(label);
    const ImVec2 size = ImGui::CalcTextSize(label, end);
    const float nudge = pressed ? static_cast<float>(t.scale) : 0.0f;
    const ImVec2 pos((min.x + max.x - size.x) / center_div + nudge, (min.y + max.y - size.y) / center_div + nudge);
    dl->AddText(rv_editor_floor(pos), rv_editor_col(rv_editor_item_text(t, item)), label, end);
}

// The shared face of every push button: a dark outline that sets it off the
// window behind it, the bevel inside, brass on hover, focus dots.
void rv_editor_button_face(ImDrawList *dl, const rv_editor_item &item, const rv_editor_theme &t, bool down)
{
    const float px = static_cast<float>(t.scale);
    const ImVec2 min(item.min.x + px, item.min.y + px);
    const ImVec2 max(item.max.x - px, item.max.y - px);
    dl->AddRectFilled(item.min, item.max, rv_editor_col(t.dark));
    rv_editor_draw_panel(dl, min, max, t, down ? t.inset : t.button,
        down ? rv_editor_bevel::sunken : rv_editor_bevel::raised);
    if (item.hovered && !item.disabled) {
        rv_editor_draw_frame(dl, min, max, t, t.selection);
    }
    if (item.focused) {
        rv_editor_draw_focus(dl, min, max, t);
    }
}

// Label to the right of a square mark: the layout of check boxes and radios.
ImVec2 rv_editor_marked_size(const char *label)
{
    const float box = ImGui::GetFrameHeight();
    const ImVec2 text = ImGui::CalcTextSize(label, rv_editor_label_end(label));
    return ImVec2(box + ImGui::GetStyle().ItemInnerSpacing.x + text.x, box);
}

void rv_editor_marked_label(ImDrawList *dl, const char *label, const rv_editor_item &item, const rv_editor_theme &t)
{
    const float box = ImGui::GetFrameHeight();
    const char *end = rv_editor_label_end(label);
    const ImVec2 text = ImGui::CalcTextSize(label, end);
    const ImVec2 pos(item.min.x + box + ImGui::GetStyle().ItemInnerSpacing.x, item.min.y + (box - text.y) / center_div);
    dl->AddText(rv_editor_floor(pos), rv_editor_col(rv_editor_item_text(t, item)), label, end);
    if (item.focused) {
        rv_editor_draw_focus(dl, ImVec2(pos.x - static_cast<float>(focus_frame_offset * t.scale), item.min.y), item.max, t);
    }
}

} // namespace

bool rv_editor_button(const char *label, const rv_editor_theme &theme, const rv_editor_state &state)
{
    const ImVec2 text = ImGui::CalcTextSize(label, rv_editor_label_end(label));
    const ImVec2 size(text.x + ImGui::GetStyle().FramePadding.x * button_pad_mult, ImGui::GetFrameHeight());
    const rv_editor_item item = rv_editor_item_add(label, size, state);
    const bool down = item.held && item.hovered;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_button_face(dl, item, theme, down);
    rv_editor_label_centred(dl, label, item.min, item.max, theme, item, down);
    return item.clicked;
}

float rv_editor_tool_button_width(const char *)
{
    // One square for every transport button: a six-letter label with padding, or
    // the letter over a line of text, whichever is larger.
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const float line = ImGui::GetTextLineHeight();
    const float ref_width = ImGui::CalcTextSize(tool_button_ref_label).x + pad.x * both_sides_mult;
    const float line_height = line * tool_button_line_mult + pad.y * tool_button_line_mult;
    return std::floor(std::max(ref_width, line_height));
}

bool rv_editor_tool_button(const char *label, const char *code, uint32_t color, const char *name,
    const char *shortcut, const rv_editor_theme &theme, const rv_editor_state &state)
{
    const float side = rv_editor_tool_button_width(label);
    const rv_editor_item item = rv_editor_item_add(label, ImVec2(side, side), state);
    const char *end = rv_editor_label_end(label);
    if (!item.disabled) {
        const std::string tip = name != nullptr ? std::string(name) : std::string(label, end);
        if (shortcut != nullptr) {
            ImGui::SetItemTooltip("%s (%s)", tip.c_str(), shortcut);
        } else {
            ImGui::SetItemTooltip("%s", tip.c_str());
        }
    }
    const bool down = item.held && item.hovered;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_button_face(dl, item, theme, down);
    const float nudge = down ? static_cast<float>(theme.scale) : 0.0f;
    const float pad = ImGui::GetStyle().FramePadding.y;
    // The code at the largest of 2x/1.5x/1x the font's size that still fits the
    // square's width, over the label, both centred across the square.
    const float fit = side - pad * both_sides_mult;
    float big = ImGui::GetFontSize() * code_font_scale_max;
    float code_w = ImGui::GetFont()->CalcTextSizeA(big, FLT_MAX, 0.0f, code).x;
    for (const float scale : { code_font_scale_mid, 1.0f }) {
        if (code_w <= fit) {
            break;
        }
        big = ImGui::GetFontSize() * scale;
        code_w = ImGui::GetFont()->CalcTextSizeA(big, FLT_MAX, 0.0f, code).x;
    }
    const float y = std::floor((item.min.y + item.max.y - big - ImGui::GetTextLineHeight() - pad) / center_div + nudge);
    // Dimmed: halfway to the window colour.
    const uint32_t dim = item.disabled ? rv_editor_blend_color(color, theme.window) : color;
    dl->AddText(ImGui::GetFont(), big, ImVec2(std::floor((item.min.x + item.max.x - code_w) / center_div + nudge), y),
        rv_editor_col(dim), code);
    const ImVec2 size = ImGui::CalcTextSize(label, end);
    dl->AddText(ImVec2(std::floor((item.min.x + item.max.x - size.x) / center_div + nudge), y + big + pad),
        rv_editor_col(rv_editor_item_text(theme, item)), label, end);
    if (item.focused) {
        rv_editor_draw_focus(dl, item.min, item.max, theme);
    }
    return item.clicked;
}

bool rv_editor_letter_button(const char *id, const char *code, uint32_t color, const char *tooltip,
    const rv_editor_theme &theme, const rv_editor_state &state)
{
    const float side = ImGui::GetFrameHeight();
    const rv_editor_item item = rv_editor_item_add(id, ImVec2(side, side), state);
    if (!item.disabled && tooltip != nullptr) {
        ImGui::SetItemTooltip("%s", tooltip);
    }
    const bool down = item.held && item.hovered;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_button_face(dl, item, theme, down);
    // Dimmed like a disabled picture: halfway to the window colour.
    const uint32_t ink = item.disabled ? rv_editor_blend_color(color, theme.window) : color;
    // The code at the default size, or smaller when it would cross the square's
    // border: a 2px inset, not FramePadding, which is too strict for a code that
    // already fits (the 5x7 bitmap font does not scale cleanly, so keep it crisp).
    const float inset = static_cast<float>(theme.scale) * letter_button_inset_mult;
    const float fit = side - inset * both_sides_mult;
    float font_size = ImGui::GetFontSize();
    ImVec2 size = ImGui::CalcTextSize(code);
    if (size.x > fit) {
        font_size *= fit / size.x;
        size = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, 0.0f, code);
    }
    const float nudge = down ? static_cast<float>(theme.scale) : 0.0f;
    dl->AddText(ImGui::GetFont(), font_size,
        ImVec2(std::floor((item.min.x + item.max.x - size.x) / center_div + nudge),
            std::floor((item.min.y + item.max.y - size.y) / center_div + nudge)),
        rv_editor_col(ink), code);
    if (item.focused) {
        rv_editor_draw_focus(dl, item.min, item.max, theme);
    }
    return item.clicked;
}

bool rv_editor_icon_button(const char *id, rv_editor_icon_name name, const rv_editor_theme &theme,
    const rv_editor_state &state)
{
    const rv_editor_icon icon = rv_editor_icon_get(name);
    const float k = static_cast<float>(rv_editor_icon_scale(theme.scale));
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    const ImVec2 art(static_cast<float>(icon.w) * k, static_cast<float>(icon.h) * k);
    const float padded_size = pad * icon_button_pad_mult;
    const rv_editor_item item = rv_editor_item_add(id, ImVec2(art.x + padded_size, art.y + padded_size), state);
    const bool down = item.held && item.hovered;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_button_face(dl, item, theme, down);
    if (icon.id != 0) {
        const float nudge = down ? static_cast<float>(theme.scale) : 0.0f;
        const ImVec2 p0 = rv_editor_floor(ImVec2(item.min.x + pad + nudge, item.min.y + pad + nudge));
        const ImU32 tint = item.disabled ? IM_COL32(ch_full, ch_full, ch_full, icon_disabled_alpha) : IM_COL32_WHITE;
        dl->AddImage(ImTextureRef(icon.id), p0, ImVec2(p0.x + art.x, p0.y + art.y), ImVec2(0, 0), ImVec2(1, 1), tint);
    }
    return item.clicked;
}

bool rv_editor_toggle(const char *label, bool *on, const rv_editor_theme &theme, const rv_editor_state &state)
{
    const ImVec2 text = ImGui::CalcTextSize(label, rv_editor_label_end(label));
    const ImVec2 size(text.x + ImGui::GetStyle().FramePadding.x * button_pad_mult, ImGui::GetFrameHeight());
    const rv_editor_item item = rv_editor_item_add(label, size, state);
    if (item.clicked) {
        *on = !*on;
    }
    const bool down = *on || (item.held && item.hovered);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_button_face(dl, item, theme, down);
    rv_editor_label_centred(dl, label, item.min, item.max, theme, item, down);
    return item.clicked;
}

bool rv_editor_checkbox(const char *label, bool *on, const rv_editor_theme &theme, const rv_editor_state &state)
{
    const rv_editor_item item = rv_editor_item_add(label, rv_editor_marked_size(label), state);
    if (item.clicked) {
        *on = !*on;
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float box = ImGui::GetFrameHeight();
    const ImVec2 box_max(item.min.x + box, item.min.y + box);
    const bool down = item.held && item.hovered;
    rv_editor_draw_panel(dl, item.min, box_max, theme, down ? theme.button : theme.inset, rv_editor_bevel::sunken);
    if (item.hovered && !item.disabled) {
        rv_editor_draw_frame(dl, item.min, box_max, theme, theme.selection);
    }
    if (*on) {
        rv_editor_draw_check(dl, item.min, box_max, theme, rv_editor_item_text(theme, item));
    }
    rv_editor_marked_label(dl, label, item, theme);
    return item.clicked;
}

bool rv_editor_radio(const char *label, bool active, const rv_editor_theme &theme, const rv_editor_state &state)
{
    const rv_editor_item item = rv_editor_item_add(label, rv_editor_marked_size(label), state);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float box = ImGui::GetFrameHeight();
    const ImVec2 box_max(item.min.x + box, item.min.y + box);
    rv_editor_draw_diamond(dl, item.min, box_max, theme, active || (item.held && item.hovered));
    if (item.hovered && !item.disabled) {
        rv_editor_draw_frame(dl, item.min, box_max, theme, theme.selection);
    }
    rv_editor_marked_label(dl, label, item, theme);
    return item.clicked;
}

float rv_editor_button_width(const char *label)
{
    return ImGui::CalcTextSize(label, rv_editor_label_end(label)).x + ImGui::GetStyle().FramePadding.x * button_pad_mult;
}

float rv_editor_checkbox_width(const char *label)
{
    return rv_editor_marked_size(label).x;
}

bool rv_editor_command_button(const char *id, const char *code, uint32_t color, const char *label,
    const char *tooltip, const rv_editor_theme &theme, const rv_editor_state &state)
{
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const float cell = ImGui::CalcTextSize(code).x;
    const float label_w = ImGui::CalcTextSize(label).x;
    const float content_width = pad.x * command_button_pad_mult + cell + label_w;
    const float width = std::max(ImGui::GetContentRegionAvail().x, content_width);
    const rv_editor_item item = rv_editor_item_add(id, ImVec2(width, ImGui::GetFrameHeight()), state);
    if (!item.disabled && tooltip != nullptr) {
        ImGui::SetItemTooltip("%s", tooltip);
    }
    const bool down = item.held && item.hovered;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_button_face(dl, item, theme, down);
    const float nudge = down ? static_cast<float>(theme.scale) : 0.0f;
    const float x = std::floor(item.min.x + pad.x + nudge);
    const float y = std::floor(item.min.y + pad.y + nudge);
    // Dimmed like a disabled picture: halfway to the window colour.
    const uint32_t ink = item.disabled ? rv_editor_blend_color(color, theme.window) : color;
    dl->AddText(ImVec2(x, y), rv_editor_col(ink), code);
    dl->AddText(ImVec2(x + cell + pad.x, y), rv_editor_col(rv_editor_item_text(theme, item)), label);
    if (item.focused) {
        rv_editor_draw_focus(dl, item.min, item.max, theme);
    }
    return item.clicked;
}

void rv_editor_flow(float width)
{
    const float right = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width;
    // After an item the cursor waits at the start of the next row: from there
    // the available width reaches the row's right edge.
    const float edge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (right <= edge) {
        ImGui::SameLine();
    }
}

} // namespace rv_editor
