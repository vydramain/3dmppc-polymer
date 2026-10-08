// The thumbwheel: a wheel seen edge-on, its ridges moving as it turns.

#include "ui/rv_editor_thumbwheel.hpp"

#include <cmath>

#include "imgui.h"

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"

namespace rv_editor
{

namespace
{

// Thumbwheel thickness as a fraction of the frame height.
constexpr float frame_thickness_ratio = 0.75f;

// Turn increment applied when an arrow key is pressed.
constexpr float key_press_delta = 8.0f;

// Large offset added before phase modulo to keep values positive across wraps.
constexpr float phase_wrap_offset = 1000.0f;

// Phase wrapping period and ridge spacing in pixels.
constexpr float ridge_spacing = 8.0f;

// Pixel margin from bevel edge when drawing the filled rectangle.
constexpr int border_margin = 2;

// Pixel distance from span end where ridge drawing stops.
constexpr float ridge_end_margin = 4.0f;

// Scale factor normalizing wheel position to range [-1, 1].
constexpr float u_normalization_scale = 2.0f;

// Half of span; ridges are positioned as 0.5*span*(1 + normalized_sine), mapping u[-1,1] to bent[0,span].
constexpr float half_span_ratio = 0.5f;

// Frequency parameter for sine wave in ridge position modulation.
constexpr float sine_frequency = 1.2f;

// Threshold for position magnitude to distinguish dark vs text tone.
constexpr float darkness_threshold = 0.7f;

// Pixel inset from edges when drawing individual ridge lines.
constexpr int ridge_line_inset = 3;

} // namespace

float rv_editor_thumbwheel(const char *id,
    const char *label,
    bool vertical,
    float length,
    const rv_editor_theme &theme,
    bool &reset)
{
    const float thick = ImGui::GetFrameHeight() * frame_thickness_ratio;
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
        if (ImGui::IsKeyPressed(more)) {
            turn += key_press_delta;
        } else if (ImGui::IsKeyPressed(less)) {
            turn -= key_press_delta;
        }
    }
    reset = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const float phase = std::fmod(storage->GetFloat(key, 0.0f) + turn + phase_wrap_offset, ridge_spacing);
    storage->SetFloat(key, phase);

    // A dark slot, the wheel in it, and ridges spaced by the turn: they move with the drag.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    rv_editor_draw_bevel(dl, p0, p1, theme, rv_editor_bevel::sunken);
    dl->AddRectFilled(ImVec2(p0.x + border_margin, p0.y + border_margin),
        ImVec2(p1.x - border_margin, p1.y - border_margin),
        rv_editor_col(theme.dark));
    const float span = vertical ? size.y : size.x;
    for (float at = phase; at < span - ridge_end_margin; at += ridge_spacing) {
        // Ridges crowd towards the ends, as on a cylinder seen side-on.
        const float u = (at / span) * u_normalization_scale - 1.0f;
        const float bent = span * half_span_ratio * (1.0f + std::sin(u * sine_frequency) / std::sin(sine_frequency));
        const uint32_t tone = std::fabs(u) > darkness_threshold ? theme.dark : theme.text;
        if (vertical) {
            dl->AddLine(ImVec2(p0.x + ridge_line_inset, p0.y + bent),
                ImVec2(p1.x - ridge_line_inset, p0.y + bent),
                rv_editor_col(tone));
        } else {
            dl->AddLine(ImVec2(p0.x + bent, p0.y + ridge_line_inset),
                ImVec2(p0.x + bent, p1.y - ridge_line_inset),
                rv_editor_col(tone));
        }
    }
    if (ImGui::IsItemFocused()) {
        rv_editor_draw_focus(dl, p0, p1, theme);
    }
    ImGui::SetItemTooltip("%s: drag or use the arrow keys; double click for home", label);
    return turn;
}

} // namespace rv_editor
