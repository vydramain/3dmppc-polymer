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

void rv_editor_pane_header(const char *title, bool active, const rv_editor_theme &theme)
{
    const float h = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + h);
    ImGui::Dummy(ImVec2(max.x - min.x, h));

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_draw_panel(dl, min, max, theme, theme.dark, rv_editor_bevel::raised);
    const float bevel = static_cast<float>(theme.bevel_px * theme.scale);
    const ImVec2 inner_min(min.x + bevel, min.y + bevel);
    const ImVec2 inner_max(max.x - bevel, max.y - bevel);
    rv_editor_draw_stipple(dl, inner_min, inner_max, theme, active ? theme.bevel_hi : theme.window);

    // The title sits on a solid patch so the stipple never runs through letters.
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    const char *end = rv_editor_label_end(title);
    const ImVec2 text = ImGui::CalcTextSize(title, end);
    const ImVec2 patch_min(inner_min.x + pad, inner_min.y);
    const ImVec2 patch_max(patch_min.x + text.x + pad * 2.0f, inner_max.y);
    dl->AddRectFilled(patch_min, patch_max, rv_editor_col(theme.dark));
    const ImVec2 pos(std::floor(patch_min.x + pad), std::floor((min.y + max.y - text.y) / 2.0f));
    dl->AddText(pos, rv_editor_col(active ? theme.text_bright : theme.text_disabled), title, end);

    // Window control box at the right end.
    const ImVec2 box_min(inner_max.x - (h - bevel * 2.0f), inner_min.y);
    rv_editor_draw_panel(dl, box_min, inner_max, theme, theme.button, rv_editor_bevel::raised);
    const float mark = std::floor((inner_max.y - inner_min.y) / 4.0f);
    const ImVec2 c(std::floor((box_min.x + inner_max.x) / 2.0f), std::floor((box_min.y + inner_max.y) / 2.0f));
    dl->AddRectFilled(ImVec2(c.x - mark / 2.0f, c.y - mark / 2.0f), ImVec2(c.x + mark / 2.0f, c.y + mark / 2.0f),
        rv_editor_col(active ? theme.text : theme.text_disabled));
}

} // namespace rv_editor
