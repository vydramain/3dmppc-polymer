// The thumbwheel: a wheel seen edge-on, its ridges moving as it turns.

#include "ui/rv_editor_thumbwheel.hpp"

#include <cmath>

#include "imgui.h"

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"

namespace rv_editor
{

float rv_editor_thumbwheel(const char *id, const char *label, bool vertical, float length, const rv_editor_theme &theme,
    bool &reset)
{
    const float thick = ImGui::GetFrameHeight() * 0.75f;
    const ImVec2 size = vertical ? ImVec2(thick, length) : ImVec2(length, thick);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const ImGuiID key = ImGui::GetItemID();
    ImGuiStorage *storage = ImGui::GetStateStorage();
    float turn = 0.0f;
    if (ImGui::IsItemActive()) {
        turn = vertical ? -ImGui::GetIO().MouseDelta.y : ImGui::GetIO().MouseDelta.x;
    }
    if (ImGui::IsItemFocused()) {
        const ImGuiKey less = vertical ? ImGuiKey_DownArrow : ImGuiKey_LeftArrow;
        const ImGuiKey more = vertical ? ImGuiKey_UpArrow : ImGuiKey_RightArrow;
        turn += ImGui::IsKeyPressed(more) ? 8.0f : ImGui::IsKeyPressed(less) ? -8.0f : 0.0f;
    }
    reset = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const float phase = std::fmod(storage->GetFloat(key, 0.0f) + turn + 1000.0f, 8.0f);
    storage->SetFloat(key, phase);

    // A dark slot, the wheel in it, and ridges spaced by the turn: they move with the drag.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    rv_editor_draw_bevel(dl, p0, p1, theme, rv_editor_bevel::sunken);
    dl->AddRectFilled(ImVec2(p0.x + 2, p0.y + 2), ImVec2(p1.x - 2, p1.y - 2), rv_editor_col(theme.dark));
    const float span = vertical ? size.y : size.x;
    for (float at = phase; at < span - 4.0f; at += 8.0f) {
        // Ridges crowd towards the ends, as on a cylinder seen side-on.
        const float u = (at / span) * 2.0f - 1.0f;
        const float bent = span * 0.5f * (1.0f + std::sin(u * 1.2f) / std::sin(1.2f));
        const uint32_t tone = std::fabs(u) > 0.7f ? theme.dark : theme.text;
        if (vertical) {
            dl->AddLine(ImVec2(p0.x + 3, p0.y + bent), ImVec2(p1.x - 3, p0.y + bent), rv_editor_col(tone));
        } else {
            dl->AddLine(ImVec2(p0.x + bent, p0.y + 3), ImVec2(p0.x + bent, p1.y - 3), rv_editor_col(tone));
        }
    }
    if (ImGui::IsItemFocused()) {
        rv_editor_draw_focus(dl, p0, p1, theme);
    }
    ImGui::SetItemTooltip("%s: drag or use the arrow keys; double click for home", label);
    return turn;
}

} // namespace rv_editor
