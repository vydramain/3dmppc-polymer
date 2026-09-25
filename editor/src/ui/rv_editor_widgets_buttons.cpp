// Buttons, icon buttons, toggles, check boxes and diamond radios.

#include <algorithm>
#include <cmath>

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

ImVec2 rv_editor_floor(ImVec2 v)
{
    return ImVec2(std::floor(v.x), std::floor(v.y));
}

// Label centred in [min, max), nudged one scaled pixel down-right when pressed.
void rv_editor_label_centred(ImDrawList *dl, const char *label, ImVec2 min, ImVec2 max, const rv_editor_theme &t,
    const rv_editor_item &item, bool pressed)
{
    const char *end = rv_editor_label_end(label);
    const ImVec2 size = ImGui::CalcTextSize(label, end);
    const float nudge = pressed ? static_cast<float>(t.scale) : 0.0f;
    const ImVec2 pos((min.x + max.x - size.x) / 2.0f + nudge, (min.y + max.y - size.y) / 2.0f + nudge);
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
    const ImVec2 pos(item.min.x + box + ImGui::GetStyle().ItemInnerSpacing.x, item.min.y + (box - text.y) / 2.0f);
    dl->AddText(rv_editor_floor(pos), rv_editor_col(rv_editor_item_text(t, item)), label, end);
    if (item.focused) {
        rv_editor_draw_focus(dl, ImVec2(pos.x - static_cast<float>(3 * t.scale), item.min.y), item.max, t);
    }
}

} // namespace

bool rv_editor_button(const char *label, const rv_editor_theme &theme, const rv_editor_state &state)
{
    const ImVec2 text = ImGui::CalcTextSize(label, rv_editor_label_end(label));
    const ImVec2 size(text.x + ImGui::GetStyle().FramePadding.x * 4.0f, ImGui::GetFrameHeight());
    const rv_editor_item item = rv_editor_item_add(label, size, state);
    const bool down = item.held && item.hovered;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_button_face(dl, item, theme, down);
    rv_editor_label_centred(dl, label, item.min, item.max, theme, item, down);
    return item.clicked;
}

float rv_editor_tool_button_width(const char *)
{
    // One square for every transport button: a six-letter label with padding,
    // or the picture over a line of text, whichever is larger.
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const float line = ImGui::GetTextLineHeight();
    return std::floor(std::max(ImGui::CalcTextSize("MMMMMM").x + pad.x * 2.0f, line * 3.0f + pad.y * 3.0f));
}

bool rv_editor_tool_button(const char *label, rv_editor_glyph glyph, uint32_t color, const char *shortcut,
    const rv_editor_theme &theme, const rv_editor_state &state)
{
    const float icon = ImGui::GetTextLineHeight() * 2.0f;
    const float pad = ImGui::GetStyle().FramePadding.y;
    const float side = rv_editor_tool_button_width(label);
    const rv_editor_item item = rv_editor_item_add(label, ImVec2(side, side), state);
    const char *end = rv_editor_label_end(label);
    if (!item.disabled && shortcut != nullptr) {
        ImGui::SetItemTooltip("%.*s (%s)", static_cast<int>(end - label), label, shortcut);
    }
    const bool down = item.held && item.hovered;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_button_face(dl, item, theme, down);
    const float nudge = down ? static_cast<float>(theme.scale) : 0.0f;
    // The picture over the label, both centred across the square.
    const float line = ImGui::GetTextLineHeight();
    const float x = std::floor((item.min.x + item.max.x - icon) / 2.0f + nudge);
    const float y = std::floor((item.min.y + item.max.y - icon - line - pad) / 2.0f + nudge);
    // Dimmed: halfway to the window colour.
    const uint32_t dim = item.disabled
        ? (((((color >> 16) & 0xff) + ((theme.window >> 16) & 0xff)) / 2) << 16) |
            (((((color >> 8) & 0xff) + ((theme.window >> 8) & 0xff)) / 2) << 8) | (((color & 0xff) + (theme.window & 0xff)) / 2)
        : color;
    rv_editor_draw_glyph(dl, ImVec2(x, y), ImVec2(x + icon, y + icon), theme, glyph, dim);
    const ImVec2 text = ImGui::CalcTextSize(label, end);
    dl->AddText(ImVec2(std::floor((item.min.x + item.max.x - text.x) / 2.0f + nudge), y + icon + pad),
        rv_editor_col(rv_editor_item_text(theme, item)), label, end);
    if (item.focused) {
        rv_editor_draw_focus(dl, item.min, item.max, theme);
    }
    return item.clicked;
}

bool rv_editor_letter_button(const char *id, char letter, uint32_t color, const char *tooltip,
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
    const uint32_t ink = item.disabled
        ? (((((color >> 16) & 0xff) + ((theme.window >> 16) & 0xff)) / 2) << 16) |
            (((((color >> 8) & 0xff) + ((theme.window >> 8) & 0xff)) / 2) << 8) | (((color & 0xff) + (theme.window & 0xff)) / 2)
        : color;
    const char text[2] = { letter, '\0' };
    const ImVec2 size = ImGui::CalcTextSize(text);
    const float nudge = down ? static_cast<float>(theme.scale) : 0.0f;
    dl->AddText(ImVec2(std::floor((item.min.x + item.max.x - size.x) / 2.0f + nudge),
                    std::floor((item.min.y + item.max.y - size.y) / 2.0f + nudge)),
        rv_editor_col(ink), text);
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
    const rv_editor_item item = rv_editor_item_add(id, ImVec2(art.x + pad * 2.0f, art.y + pad * 2.0f), state);
    const bool down = item.held && item.hovered;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_button_face(dl, item, theme, down);
    if (icon.id != 0) {
        const float nudge = down ? static_cast<float>(theme.scale) : 0.0f;
        const ImVec2 p0 = rv_editor_floor(ImVec2(item.min.x + pad + nudge, item.min.y + pad + nudge));
        const ImU32 tint = item.disabled ? IM_COL32(255, 255, 255, 96) : IM_COL32_WHITE;
        dl->AddImage(ImTextureRef(icon.id), p0, ImVec2(p0.x + art.x, p0.y + art.y), ImVec2(0, 0), ImVec2(1, 1), tint);
    }
    return item.clicked;
}

bool rv_editor_toggle(const char *label, bool *on, const rv_editor_theme &theme, const rv_editor_state &state)
{
    const ImVec2 text = ImGui::CalcTextSize(label, rv_editor_label_end(label));
    const ImVec2 size(text.x + ImGui::GetStyle().FramePadding.x * 4.0f, ImGui::GetFrameHeight());
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
    return ImGui::CalcTextSize(label, rv_editor_label_end(label)).x + ImGui::GetStyle().FramePadding.x * 4.0f;
}

float rv_editor_checkbox_width(const char *label)
{
    return rv_editor_marked_size(label).x;
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
