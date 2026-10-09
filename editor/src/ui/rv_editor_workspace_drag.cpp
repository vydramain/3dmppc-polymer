// Tile drag and drop: move a header or a tab to another place in the
// tiled workspace, without recreating the moved pane.

#include <algorithm>
#include <string>

#include "imgui.h"

#include "pdk/rv_err.h"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Edge band width for dock zone detection, as a fraction of the smaller tile dimension.
constexpr float edge_band_ratio = 0.3f;

// Drag movement threshold before drag activates, in scale-relative units (multiplied by theme.scale).
constexpr float drag_threshold_base = 4.0f;

// Split ratio for dock preview rectangle: half the tile width or height.
constexpr float dock_preview_split_ratio = 0.5f;

// Dock preview overlay alpha bits: 0x90 is about 56% opacity.
constexpr uint32_t dock_preview_alpha_bits = 0x90000000u;

// The leaf whose placed rectangle contains `pos`, or rv_editor_tile_none.
uint32_t rv_editor_tile_leaf_at(const rv_editor_layout &layout, const std::vector<rv_editor_rect> &rect_of, ImVec2 pos)
{
    for (uint32_t i = 0; i < rect_of.size(); ++i) {
        if (layout.nodes[i].kind != rv_editor_tile_kind::leaf) {
            continue;
        }
        const rv_editor_rect &r = rect_of[i];
        if (pos.x >= r.x && pos.x <= r.x + r.w && pos.y >= r.y && pos.y <= r.y + r.h) {
            return i;
        }
    }
    return rv_editor_tile_none;
}

// Which zone `pos` falls in over `leaf`'s rectangle `rect`: an edge band near
// a side, or the whole tile for the tab strip / centre. Bands measure the
// whole leaf rect, not its content sub-rect (header/tabs chrome excluded).
bool rv_editor_tile_drop_zone(const rv_editor_layout &layout,
    uint32_t leaf,
    const rv_editor_rect &rect,
    ImVec2 pos,
    rv_editor_tile_dock &dock)
{
    const float x = pos.x - static_cast<float>(rect.x);
    const float y = pos.y - static_cast<float>(rect.y);
    if (x < 0.0f || y < 0.0f || x > rect.w || y > rect.h) {
        return false;
    }
    // The visible tab strip always reads as a tab drop, even inside the bottom band.
    if (layout.nodes[leaf].leaf.tabs.size() > 1 && y >= static_cast<float>(rect.h) - ImGui::GetFrameHeight()) {
        dock = rv_editor_tile_dock::tab;
        return true;
    }
    const float band = std::min(static_cast<float>(rect.w), static_cast<float>(rect.h)) * edge_band_ratio;
    const float d_left = x;
    const float d_right = static_cast<float>(rect.w) - x;
    const float d_top = y;
    const float d_bottom = static_cast<float>(rect.h) - y;
    const float nearest = std::min({ d_left, d_right, d_top, d_bottom });
    if (nearest > band) {
        dock = rv_editor_tile_dock::tab;
        return true;
    }
    if (nearest == d_left) {
        dock = rv_editor_tile_dock::left;
    } else if (nearest == d_right) {
        dock = rv_editor_tile_dock::right;
    } else if (nearest == d_top) {
        dock = rv_editor_tile_dock::top;
    } else {
        dock = rv_editor_tile_dock::bottom;
    }
    return true;
}

// A drop target: the leaf under the pointer and the side the pane docks to.
struct rv_editor_tile_zone {
    uint32_t leaf = rv_editor_tile_none;
    rv_editor_tile_dock dock = rv_editor_tile_dock::tab;
};

// Whether a drag is armed or in progress.
bool rv_editor_tile_drag_running(const rv_editor_workspace &ws)
{
    return ws.drag.armed || ws.drag.dragging;
}

// Whether the pointer is at least the drag threshold away from the press point.
bool rv_editor_tile_drag_moved(const rv_editor_workspace &ws, const rv_editor_theme &theme)
{
    const ImVec2 pos = ImGui::GetMousePos();
    const float dx = pos.x - ws.drag.press.x;
    const float dy = pos.y - ws.drag.press.y;
    const float threshold = drag_threshold_base * theme.scale;
    return dx * dx + dy * dy >= threshold * threshold;
}

// The drop zone under the pointer; its leaf is rv_editor_tile_none when there is none.
rv_editor_tile_zone rv_editor_tile_drag_zone_at(const rv_editor_workspace &ws, const std::vector<rv_editor_rect> &rect_of)
{
    const ImVec2 pos = ImGui::GetMousePos();
    rv_editor_tile_zone zone;
    zone.leaf = rv_editor_tile_leaf_at(ws.layout, rect_of, pos);
    if (zone.leaf != rv_editor_tile_none &&
        !rv_editor_tile_drop_zone(ws.layout, zone.leaf, rect_of[zone.leaf], pos, zone.dock)) {
        zone.leaf = rv_editor_tile_none;
    }
    return zone;
}

// The drag passes the threshold: the drop-zone preview starts showing.
void rv_editor_tile_drag_start(rv_editor_workspace &ws)
{
    ws.drag.dragging = true;
}

// Why a drop onto a leaf that holds only the dragged pane is refused.
constexpr const char *drop_refused_reason = "the pane stays where it is: a side drop needs a second tab in its own leaf, "
                                            "and the target must be a leaf";

// The dragged pane moves to `zone`, and the drag ends. On refusal `error` says why.
int rv_editor_tile_drag_drop(rv_editor_workspace &ws, const rv_editor_tile_zone &zone, std::string &error)
{
    const int rv = rv_editor_tile_move(ws.layout, ws.drag.pane, zone.leaf, zone.dock);
    if (rv != RV_OK) {
        error = drop_refused_reason;
    }
    ws.drag = rv_editor_tile_drag{};
    return rv;
}

// The drag ends with no change.
void rv_editor_tile_drag_cancel(rv_editor_workspace &ws)
{
    ws.drag = rv_editor_tile_drag{};
}

// Shows the drop-zone preview: the half the pane would take, or the whole tile for a tab.
void rv_editor_tile_drag_preview(const std::vector<rv_editor_rect> &rect_of,
    const rv_editor_tile_zone &zone,
    const rv_editor_theme &theme)
{
    const rv_editor_rect &r = rect_of[zone.leaf];
    ImVec2 pmin(static_cast<float>(r.x), static_cast<float>(r.y));
    ImVec2 pmax(static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h));
    switch (zone.dock) {
    case rv_editor_tile_dock::left:
        pmax.x = pmin.x + static_cast<float>(r.w) * dock_preview_split_ratio;
        break;
    case rv_editor_tile_dock::right:
        pmin.x = pmax.x - static_cast<float>(r.w) * dock_preview_split_ratio;
        break;
    case rv_editor_tile_dock::top:
        pmax.y = pmin.y + static_cast<float>(r.h) * dock_preview_split_ratio;
        break;
    case rv_editor_tile_dock::bottom:
        pmin.y = pmax.y - static_cast<float>(r.h) * dock_preview_split_ratio;
        break;
    default:
        break;
    }
    const ImU32 fill = (rv_editor_col(theme.selection) & ~IM_COL32_A_MASK) | dock_preview_alpha_bits;
    ImGui::GetForegroundDrawList()->AddRectFilled(pmin, pmax, fill);
}

} // namespace

void rv_editor_tile_drag_header(rv_editor_workspace &ws, uint32_t leaf, bool title_pressed)
{
    if (!title_pressed || ws.drag.armed || ws.drag.dragging) {
        return;
    }
    const rv_editor_tile_leaf &tabs = ws.layout.nodes[leaf].leaf;
    if (tabs.tabs.empty()) {
        return;
    }
    ws.drag.armed = true;
    ws.drag.pane = tabs.tabs[tabs.active];
    ws.drag.from_leaf = leaf;
    ws.drag.press = ImGui::GetMousePos();
}

void rv_editor_tile_drag_tabs(rv_editor_workspace &ws, uint32_t leaf, int pressed_tab)
{
    if (pressed_tab < 0 || ws.drag.armed || ws.drag.dragging) {
        return;
    }
    const rv_editor_tile_leaf &tabs = ws.layout.nodes[leaf].leaf;
    if (static_cast<size_t>(pressed_tab) >= tabs.tabs.size()) {
        return;
    }
    ws.drag.armed = true;
    ws.drag.pane = tabs.tabs[static_cast<size_t>(pressed_tab)];
    ws.drag.from_leaf = leaf;
    ws.drag.press = ImGui::GetMousePos();
}

int rv_editor_tile_drag_update(rv_editor_workspace &ws,
    const std::vector<rv_editor_rect> &rect_of,
    const rv_editor_theme &theme,
    std::string &error)
{
    if (!rv_editor_tile_drag_running(ws)) {
        return RV_OK;
    }
    const rv_editor_tile_zone zone = rv_editor_tile_drag_zone_at(ws, rect_of);
    const bool over_zone = zone.leaf != rv_editor_tile_none;
    // Dropping a tab on its own leaf's tab strip is no move: the pane stays, so it cancels.
    const bool onto_own_tabs = zone.leaf == ws.drag.from_leaf && zone.dock == rv_editor_tile_dock::tab;
    const bool drops = ws.drag.dragging && over_zone && !onto_own_tabs;

    // Release ends the drag; Escape cancels it; otherwise the drag may start and its preview is drawn in the same frame.
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (drops) {
            return rv_editor_tile_drag_drop(ws, zone, error);
        }
        rv_editor_tile_drag_cancel(ws);
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        rv_editor_tile_drag_cancel(ws);
    } else {
        if (!ws.drag.dragging && rv_editor_tile_drag_moved(ws, theme)) {
            rv_editor_tile_drag_start(ws);
        }
        if (ws.drag.dragging && over_zone) {
            rv_editor_tile_drag_preview(rect_of, zone, theme);
        }
    }
    return RV_OK;
}

} // namespace rv_editor
