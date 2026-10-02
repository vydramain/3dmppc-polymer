// Tile drag and drop: move a header or a tab to another place in the
// tiled workspace, without recreating the moved pane.

#include <algorithm>

#include "imgui.h"

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

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
bool rv_editor_tile_drop_zone(const rv_editor_layout &layout, uint32_t leaf, const rv_editor_rect &rect, ImVec2 pos,
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
    const float band = std::min(static_cast<float>(rect.w), static_cast<float>(rect.h)) * 0.3f;
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

void rv_editor_tile_drag_update(rv_editor_workspace &ws, const std::vector<rv_editor_rect> &rect_of,
    const rv_editor_theme &theme)
{
    rv_editor_tile_drag &drag = ws.drag;
    if (!drag.armed && !drag.dragging) {
        return;
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (drag.dragging) {
            const ImVec2 pos = ImGui::GetMousePos();
            const uint32_t target = rv_editor_tile_leaf_at(ws.layout, rect_of, pos);
            rv_editor_tile_dock dock = rv_editor_tile_dock::tab;
            if (target != rv_editor_tile_none && rv_editor_tile_drop_zone(ws.layout, target, rect_of[target], pos, dock)) {
                rv_editor_tile_move(ws.layout, drag.pane, target, dock);
            }
        }
        drag = rv_editor_tile_drag{};
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        drag = rv_editor_tile_drag{};
        return;
    }

    const ImVec2 pos = ImGui::GetMousePos();
    if (!drag.dragging) {
        const float dx = pos.x - drag.press.x;
        const float dy = pos.y - drag.press.y;
        const float threshold = 4.0f * theme.scale;
        if (dx * dx + dy * dy < threshold * threshold) {
            return;
        }
        drag.dragging = true;
    }

    const uint32_t target = rv_editor_tile_leaf_at(ws.layout, rect_of, pos);
    rv_editor_tile_dock dock = rv_editor_tile_dock::tab;
    if (target == rv_editor_tile_none || !rv_editor_tile_drop_zone(ws.layout, target, rect_of[target], pos, dock)) {
        return;
    }

    // The half the pane would take, or the whole tile for a tab.
    const rv_editor_rect &r = rect_of[target];
    ImVec2 pmin(static_cast<float>(r.x), static_cast<float>(r.y));
    ImVec2 pmax(static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h));
    if (dock == rv_editor_tile_dock::left) {
        pmax.x = pmin.x + static_cast<float>(r.w) * 0.5f;
    } else if (dock == rv_editor_tile_dock::right) {
        pmin.x = pmax.x - static_cast<float>(r.w) * 0.5f;
    } else if (dock == rv_editor_tile_dock::top) {
        pmax.y = pmin.y + static_cast<float>(r.h) * 0.5f;
    } else if (dock == rv_editor_tile_dock::bottom) {
        pmin.y = pmax.y - static_cast<float>(r.h) * 0.5f;
    }
    const ImU32 fill = (rv_editor_col(theme.selection) & 0x00ffffffu) | 0x90000000u;
    ImGui::GetForegroundDrawList()->AddRectFilled(pmin, pmax, fill);
}

} // namespace rv_editor
