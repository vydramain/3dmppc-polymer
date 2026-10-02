// The shelf/well layers every pane reads: a raised strip for buttons over a
// sunken area for content, under the pane's own dark header.

#include <algorithm>
#include <vector>

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// The theme has no strip token of its own: the shelf's tone is the window
// blended a quarter of the way toward the raised edge's light colour.
uint32_t rv_editor_shelf_fill(const rv_editor_theme &theme)
{
    const auto lerp8 = [](int a, int b) { return static_cast<uint32_t>(a + (b - a) / 4); };
    const int wr = (theme.window >> 16) & 0xff, wg = (theme.window >> 8) & 0xff, wb = theme.window & 0xff;
    const int hr = (theme.bevel_hi >> 16) & 0xff, hg = (theme.bevel_hi >> 8) & 0xff, hb = theme.bevel_hi & 0xff;
    return (lerp8(wr, hr) << 16) | (lerp8(wg, hg) << 8) | lerp8(wb, hb);
}

// The well has no token of its own either: halfway between the inset tone and
// the theme's darkest surface, so it reads as clearly deeper than the shelf.
uint32_t rv_editor_well_fill(const rv_editor_theme &theme)
{
    const auto lerp8 = [](int a, int b) { return static_cast<uint32_t>((a + b) / 2); };
    const int ir = (theme.inset >> 16) & 0xff, ig = (theme.inset >> 8) & 0xff, ib = theme.inset & 0xff;
    const int dr = (theme.dark >> 16) & 0xff, dg = (theme.dark >> 8) & 0xff, db = theme.dark & 0xff;
    return (lerp8(ir, dr) << 16) | (lerp8(ig, dg) << 8) | lerp8(ib, db);
}

struct rv_editor_shelf_frame
{
    ImVec2 origin; // screen pos before padding
    float width;   // full strip width
    float pad;
    rv_editor_theme theme;
};

std::vector<rv_editor_shelf_frame> rv_editor_shelf_stack;

struct rv_editor_well_frame
{
    ImVec2 min; // outer rect, bevel and padding included
    ImVec2 max;
};

std::vector<rv_editor_well_frame> rv_editor_well_stack;

} // namespace

void rv_editor_shelf_begin(const char *id, const rv_editor_theme &theme)
{
    ImGui::PushID(id);
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    // Channel 1 (content) is measured before channel 0 (the strip) is drawn,
    // since the strip's height follows the content's.
    ImGui::GetWindowDrawList()->ChannelsSplit(2);
    ImGui::GetWindowDrawList()->ChannelsSetCurrent(1);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, origin.y + pad));
    ImGui::BeginGroup();
    rv_editor_shelf_stack.push_back({ origin, width, pad, theme });
}

void rv_editor_shelf_end()
{
    const rv_editor_shelf_frame f = rv_editor_shelf_stack.back();
    rv_editor_shelf_stack.pop_back();
    ImGui::EndGroup();
    const ImVec2 min = f.origin;
    const ImVec2 max(min.x + f.width, ImGui::GetItemRectMax().y + f.pad);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->ChannelsSetCurrent(0);
    rv_editor_draw_panel(dl, min, max, f.theme, rv_editor_shelf_fill(f.theme), rv_editor_bevel::raised);
    dl->ChannelsMerge();

    // The whole strip as one item, so what follows starts under it.
    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(ImVec2(max.x - min.x, max.y - min.y));
    ImGui::PopID();
}

bool rv_editor_well_begin(const char *id, ImVec2 size, const rv_editor_theme &theme, ImGuiWindowFlags flags)
{
    ImGui::PushID(id);
    const float pad = static_cast<float>(theme.pad_px * theme.scale);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    // As for BeginChild: 0 takes what is left, a negative size leaves that much.
    const ImVec2 max(min.x + (size.x > 0.0f ? size.x : avail.x + size.x),
        min.y + (size.y > 0.0f ? size.y : avail.y + size.y));

    ImDrawList *dl = ImGui::GetWindowDrawList();
    rv_editor_draw_panel(dl, min, max, theme, rv_editor_well_fill(theme), rv_editor_bevel::sunken);
    rv_editor_well_stack.push_back({ min, max });

    const ImVec2 inner(std::max(1.0f, max.x - min.x - 2.0f * pad), std::max(1.0f, max.y - min.y - 2.0f * pad));
    ImGui::SetCursorScreenPos(ImVec2(min.x + pad, min.y + pad));
    // Transparent, so the well's fill shows through this child and any child
    // nested in it (e.g. a scroll area), instead of ImGui's own ChildBg.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    return ImGui::BeginChild("##well", inner, ImGuiChildFlags_None, flags);
}

void rv_editor_well_end()
{
    const rv_editor_well_frame f = rv_editor_well_stack.back();
    rv_editor_well_stack.pop_back();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    // The whole well as one item, bevel and padding included, as after a child.
    ImGui::SetCursorScreenPos(f.min);
    ImGui::Dummy(ImVec2(f.max.x - f.min.x, f.max.y - f.min.y));
    ImGui::PopID();
}

} // namespace rv_editor
