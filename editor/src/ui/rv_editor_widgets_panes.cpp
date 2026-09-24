// Splitters and pane headers: the pieces the tiled workspace is built from.

#include <algorithm>
#include <cmath>

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

bool rv_editor_splitter(const char *id, rv_editor_axis axis, float length, float *a, float *b, float min_a, float min_b,
    const rv_editor_theme &theme, const rv_editor_state &state)
{
    const float thickness = static_cast<float>(theme.pad_px * theme.scale);
    const bool across_x = axis == rv_editor_axis::x;
    const ImVec2 size = across_x ? ImVec2(thickness, length) : ImVec2(length, thickness);
    const rv_editor_item item = rv_editor_item_add(id, size, state);

    bool changed = false;
    if (item.held && state.look == rv_editor_look::live) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        // Move the boundary, but never past either side's minimum.
        float d = across_x ? delta.x : delta.y;
        d = std::max(d, min_a - *a);
        d = std::min(d, *b - min_b);
        if (d != 0.0f) {
            *a += d;
            *b -= d;
            changed = true;
        }
    }
    if ((item.hovered || item.held) && !item.disabled) {
        ImGui::SetMouseCursor(across_x ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_draw_panel(dl, item.min, item.max, theme, item.held ? theme.selection : theme.window,
        rv_editor_bevel::raised);
    if (item.hovered && !item.held && !item.disabled) {
        rv_editor_draw_frame(dl, item.min, item.max, theme, theme.selection);
    }
    return changed;
}

namespace
{

// One letter box of a pane header: a small button face with hover, press and focus.
bool rv_editor_header_box(const char *id, const char *letter, ImVec2 pos, float size, bool active,
    const rv_editor_theme &theme, const rv_editor_state &state)
{
    ImGui::SetCursorScreenPos(pos);
    const rv_editor_item item = rv_editor_item_add(id, ImVec2(size, size), state);
    const bool down = item.held && item.hovered;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_draw_panel(dl, item.min, item.max, theme, down ? theme.inset : theme.button,
        down ? rv_editor_bevel::sunken : rv_editor_bevel::raised);
    if (item.hovered && !item.disabled) {
        rv_editor_draw_frame(dl, item.min, item.max, theme, theme.selection);
    }
    if (item.focused) {
        rv_editor_draw_focus(dl, item.min, item.max, theme);
    }
    const ImVec2 text = ImGui::CalcTextSize(letter);
    const float nudge = down ? static_cast<float>(theme.scale) : 0.0f;
    const ImVec2 at(std::floor((item.min.x + item.max.x - text.x) / 2.0f + nudge),
        std::floor((item.min.y + item.max.y - text.y) / 2.0f + nudge));
    dl->AddText(at, rv_editor_col(active ? theme.text : theme.text_disabled), letter);
    return item.clicked;
}

} // namespace

rv_editor_header_action rv_editor_pane_header(const char *title, bool active, const rv_editor_theme &theme,
    bool controls, const rv_editor_state &state)
{
    const float h = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + h);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_draw_panel(dl, min, max, theme, theme.dark, rv_editor_bevel::raised);
    const float bevel = static_cast<float>(theme.bevel_px * theme.scale);
    const ImVec2 inner_min(min.x + bevel, min.y + bevel);
    const ImVec2 inner_max(max.x - bevel, max.y - bevel);
    rv_editor_draw_stipple(dl, inner_min, inner_max, theme, active ? theme.bevel_hi : theme.window);

    rv_editor_header_action action = rv_editor_header_action::none;
    float title_x = inner_min.x;
    if (controls) {
        const float box = inner_max.y - inner_min.y;
        ImGui::PushID(title);
        if (rv_editor_header_box("##close", "X", inner_min, box, active, theme, state)) {
            action = rv_editor_header_action::close;
        }
        const ImVec2 right(inner_max.x - box, inner_min.y);
        if (rv_editor_header_box("##maximize", "M", right, box, active, theme, state)) {
            action = rv_editor_header_action::maximize;
        }
        ImGui::PopID();
        title_x += box;
    }

    // The title sits on a solid patch so the stipple never runs through letters.
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    const char *end = rv_editor_label_end(title);
    const ImVec2 text = ImGui::CalcTextSize(title, end);
    const ImVec2 patch_min(title_x + pad, inner_min.y);
    const ImVec2 patch_max(patch_min.x + text.x + pad * 2.0f, inner_max.y);
    dl->AddRectFilled(patch_min, patch_max, rv_editor_col(theme.dark));
    const ImVec2 pos(std::floor(patch_min.x + pad), std::floor((min.y + max.y - text.y) / 2.0f));
    dl->AddText(pos, rv_editor_col(active ? theme.text_bright : theme.text_disabled), title, end);

    // The bar as one layout item, so what follows starts below it.
    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(ImVec2(max.x - min.x, h));
    return action;
}

} // namespace rv_editor
