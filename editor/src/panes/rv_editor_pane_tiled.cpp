#include "panes/rv_editor_pane_tiled.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "imgui_internal.h" // an item added without taking room

#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_facts.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

namespace
{

// The frames of the areas begun and not yet ended, and whether each is a facts list.
struct rv_editor_tiled_frame {
    ImRect rect;
    ImGuiID key; // the area's own id, so two areas of a pane have frames of their own
    bool facts;
};
std::vector<rv_editor_tiled_frame> rv_editor_tiled_frames;

// Padding edges around a shelf's row (one per side).
constexpr float shelf_pad_sides = 2.0f;

// The floors as given, or the one default floor.
std::vector<float> rv_editor_tiled_floors(std::initializer_list<float> floors)
{
    return floors.size() == 0 ? std::vector<float>{ rv_editor_tiled_floor() } : std::vector<float>(floors);
}

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

float rv_editor_tiled_floor()
{
    // The widget's floor holds three rows inside the frame, and the area window inside (the frame less the theme's pad, which
    // is the style's horizontal frame padding, on both sides) is never shorter than that floor.
    return rv_editor_content_min_height() + shelf_pad_sides * ImGui::GetStyle().FramePadding.x;
}

float rv_editor_tiled_table_pitch()
{
    const ImGuiStyle &style = ImGui::GetStyle();
    return ImGui::GetFrameHeight() + 2.0f * style.CellPadding.y + style.ItemSpacing.y;
}

float rv_editor_tiled_rows_floor(float rows, float pitch)
{
    return rv_editor_tiled_floor() - 3.0f * ImGui::GetTextLineHeightWithSpacing() + rows * pitch;
}

rv_editor_size rv_editor_tiled_minimum(float content_w, float above_h, std::initializer_list<float> floors)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const std::vector<float> all = rv_editor_tiled_floors(floors);
    float areas = static_cast<float>(all.size() - 1) * style.ItemSpacing.y;
    for (const float f : all) {
        areas += f;
    }
    return { static_cast<int32_t>(std::ceil(style.WindowPadding.x + content_w)),
        static_cast<int32_t>(std::ceil(style.WindowPadding.y + above_h + areas)) };
}

bool rv_editor_tiled_area_begin(const char *id,
    float content_w,
    const rv_editor_theme &theme,
    std::initializer_list<float> floors,
    bool facts)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const std::vector<float> all = rv_editor_tiled_floors(floors);
    float need = static_cast<float>(all.size() - 1) * ImGui::GetStyle().ItemSpacing.y;
    for (const float f : all) {
        need += f;
    }
    // The last area takes what is left; the ones above it a whole share, so the rounding goes to the last.
    const float extra = std::max(0.0f, avail.y - need);
    const float share = all.size() > 1 ? std::floor(extra / static_cast<float>(all.size())) : extra;
    const ImVec2 size(std::max(avail.x, content_w), all[0] + share);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    rv_editor_tiled_frames.push_back({ ImRect(at, ImVec2(at.x + size.x, at.y + size.y)), ImGui::GetID(id), facts });
    if (facts) {
        rv_editor_facts_begin(id, size, theme);
        return true;
    }
    return rv_editor_content_begin(id, size, theme);
}

void rv_editor_tiled_area_end(const rv_editor_theme &theme)
{
    const rv_editor_tiled_frame open = rv_editor_tiled_frames.back();
    const ImRect frame = open.rect;
    rv_editor_tiled_frames.pop_back();
    if (open.facts) {
        rv_editor_facts_end(theme);
    } else {
        rv_editor_content_end(theme);
    }
    // The content's own rectangles stop short of its frame; this one does not.
    ImGui::ItemAdd(frame, ImHashStr("##frame", 0, open.key), nullptr, ImGuiItemFlags_NoNav);
}

} // namespace rv_editor
