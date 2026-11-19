#include "panes/rv_editor_pane_tiled.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "imgui_internal.h" // an item added without taking room

#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

namespace
{

// The frames of the areas begun and not yet ended.
std::vector<ImRect> rv_editor_tiled_frames;

// Padding edges around a shelf's row (one per side).
constexpr float shelf_pad_sides = 2.0f;

} // namespace

float rv_editor_tiled_shelf_block()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    // The style's frame padding is the theme's pad, which is also the shelf's.
    return shelf_pad_sides * style.FramePadding.x + ImGui::GetFrameHeight() + style.ItemSpacing.y;
}

float rv_editor_tiled_box_block()
{
    return rv_editor_message_box_height() + ImGui::GetStyle().ItemSpacing.y;
}

float rv_editor_tiled_shelf_width(float items)
{
    return items + shelf_pad_sides * ImGui::GetStyle().FramePadding.x;
}

rv_editor_size rv_editor_tiled_minimum(float content_w, float above_h)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    return { static_cast<int32_t>(std::ceil(style.WindowPadding.x + content_w)),
        static_cast<int32_t>(std::ceil(style.WindowPadding.y + above_h + rv_editor_content_min_height())) };
}

bool rv_editor_tiled_area_begin(const char *id, float content_w, const rv_editor_theme &theme)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 size(std::max(avail.x, content_w), std::max(avail.y, rv_editor_content_min_height()));
    const ImVec2 at = ImGui::GetCursorScreenPos();
    rv_editor_tiled_frames.push_back(ImRect(at, ImVec2(at.x + size.x, at.y + size.y)));
    return rv_editor_content_begin(id, size, theme);
}

void rv_editor_tiled_area_end(const rv_editor_theme &theme)
{
    const ImRect frame = rv_editor_tiled_frames.back();
    rv_editor_tiled_frames.pop_back();
    rv_editor_content_end(theme);
    // The content's own rectangles stop short of its frame; this one does not.
    ImGui::ItemAdd(frame, ImGui::GetID("##frame"), nullptr, ImGuiItemFlags_NoNav);
}

} // namespace rv_editor
