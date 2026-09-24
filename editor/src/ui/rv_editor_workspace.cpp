// Tiled workspace: layout, splitting, tab rows, maximize, focus.

#include <algorithm>
#include <cstdio>
#include <vector>

#include "imgui.h"

#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

const char *rv_editor_pane_title(rv_editor_pane_kind kind)
{
    switch (kind) {
        case rv_editor_pane_kind::empty: return "Empty";
        case rv_editor_pane_kind::catalog: return "Widget Catalog";
        case rv_editor_pane_kind::project: return "Project";
        case rv_editor_pane_kind::files: return "Files";
        case rv_editor_pane_kind::assets: return "Assets";
        case rv_editor_pane_kind::scene: return "Scene";
        case rv_editor_pane_kind::hierarchy: return "Hierarchy";
        case rv_editor_pane_kind::inspector: return "Inspector";
        case rv_editor_pane_kind::game: return "Game";
        case rv_editor_pane_kind::code: return "Code";
        case rv_editor_pane_kind::controls: return "Runtime Controls";
        case rv_editor_pane_kind::run_config: return "Run Configuration";
        case rv_editor_pane_kind::output: return "Output";
        case rv_editor_pane_kind::terminal: return "Terminal";
        case rv_editor_pane_kind::problems: return "Problems";
        case rv_editor_pane_kind::search: return "Search Results";
        case rv_editor_pane_kind::toolchest: return "Toolchest";
        case rv_editor_pane_kind::console: return "Console";
    }
    return "?";
}

namespace
{

// A change to the tree, recorded while drawing and applied after it.
struct rv_editor_tile_action
{
    enum class op { none, split, set_kind, maximize, close, close_leaf } what = op::none;
    uint32_t leaf = rv_editor_tile_none; // the leaf acted on (split, maximize)
    rv_editor_pane_id pane = rv_editor_tile_none;
    rv_editor_pane_kind kind = rv_editor_pane_kind::empty;
    rv_editor_tile_dock dock = rv_editor_tile_dock::tab;
};

// The leaf's panes as the catalog's folder tabs; a click makes that pane the active one.
void draw_tabs(rv_editor_workspace &ws, uint32_t node, const rv_editor_theme &theme)
{
    const rv_editor_tile_leaf &leaf = ws.layout.nodes[node].leaf;
    std::vector<const char *> labels;
    for (const rv_editor_pane_id pane : leaf.tabs) {
        labels.push_back(rv_editor_pane_title(ws.panes.panes[pane].kind));
    }
    int active = static_cast<int>(leaf.active);
    if (rv_editor_tab_strip("##tabs", labels.data(), static_cast<int>(labels.size()), &active, theme)) {
        rv_editor_tile_activate(ws.layout, leaf.tabs[static_cast<size_t>(active)]);
    }
}

void draw_leaf(rv_editor_workspace &ws, uint32_t node, rv_editor_rect rect, const rv_editor_theme &theme,
    rv_editor_pane_draw_fn draw_pane, rv_editor_tile_action &action)
{
    const auto &leaf = ws.layout.nodes[node].leaf;
    const float s = theme.scale;
    const float bevel = theme.bevel_px * s;
    // The tile is a window: a raised frame, the pane header, the padded content
    // and, when the leaf holds more than one pane, folder tabs under it.
    const ImVec2 outer_min(static_cast<float>(rect.x), static_cast<float>(rect.y));
    const ImVec2 outer_max(outer_min.x + rect.w, outer_min.y + rect.h);
    rv_editor_draw_panel(ImGui::GetWindowDrawList(), outer_min, outer_max, theme, theme.window,
        rv_editor_bevel::raised);

    ImGui::SetCursorScreenPos(ImVec2(outer_min.x + bevel, outer_min.y + bevel));
    ImGui::PushID(node);
    ImGui::BeginChild("##leaf", ImVec2(rect.w - 2.0f * bevel, rect.h - 2.0f * bevel), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const rv_editor_pane_id active = leaf.tabs.empty() ? rv_editor_tile_none : leaf.tabs[leaf.active];
    const char *title = active == rv_editor_tile_none ? "Empty" : rv_editor_pane_title(ws.panes.panes[active].kind);
    // X takes the tile off the screen, M maximizes it.
    const rv_editor_header_action clicked = rv_editor_pane_header(title, node == ws.focused_leaf, theme, true);
    if (clicked == rv_editor_header_action::close) {
        action.what = rv_editor_tile_action::op::close_leaf;
        action.leaf = node;
    } else if (clicked == rv_editor_header_action::maximize) {
        action.what = rv_editor_tile_action::op::maximize;
        action.leaf = node;
    }

    // The header and the tab strip: a right click there opens the tile's menu.
    const ImVec2 row_min = outer_min;
    const ImVec2 row_max(outer_max.x, ImGui::GetCursorScreenPos().y);

    // Context menu.
    if (ImGui::IsMouseHoveringRect(row_min, row_max) && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("##tile");
    }
    if (ImGui::BeginPopup("##tile")) {
        if (ImGui::MenuItem("Split Right")) {
            action.what = rv_editor_tile_action::op::split;
            action.leaf = node;
            action.dock = rv_editor_tile_dock::right;
        }
        if (ImGui::MenuItem("Split Down")) {
            action.what = rv_editor_tile_action::op::split;
            action.leaf = node;
            action.dock = rv_editor_tile_dock::bottom;
        }
        if (ImGui::BeginMenu("Change To", active != rv_editor_tile_none)) {
            for (uint32_t k = 0; k <= static_cast<uint32_t>(rv_editor_pane_kind::console); ++k) {
                const auto kind = static_cast<rv_editor_pane_kind>(k);
                const char *label = rv_editor_pane_title(kind);
                const bool selected = (ws.panes.panes[active].kind == kind);
                if (ImGui::MenuItem(label, nullptr, selected)) {
                    action.what = rv_editor_tile_action::op::set_kind;
                    action.pane = active;
                    action.kind = kind;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ws.layout.maximized_leaf == node ? "Restore" : "Maximize")) {
            action.what = rv_editor_tile_action::op::maximize;
            action.leaf = node;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Close", nullptr, false, active != rv_editor_tile_none)) {
            action.what = rv_editor_tile_action::op::close;
            action.pane = active;
        }
        ImGui::EndPopup();
    }

    // The content keeps the theme's padding off the frame, like every catalog pane.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(theme.pad_px * s, theme.pad_px * s));
    const bool tabbed = leaf.tabs.size() > 1;
    const float tabs_h = tabbed ? ImGui::GetFrameHeight() : 0.0f;
    ImGui::BeginChild("##pane", ImVec2(0, -tabs_h), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    // A sunken well inside the raised frame: the window's double edge. Drawn by
    // the content window itself, whose background would cover the parent's lines.
    const ImVec2 well_min = ImGui::GetWindowPos();
    const ImVec2 well_max(well_min.x + ImGui::GetWindowWidth(), well_min.y + ImGui::GetWindowHeight());
    rv_editor_draw_bevel(ImGui::GetWindowDrawList(), well_min, well_max, theme, rv_editor_bevel::sunken);
    if (!leaf.tabs.empty()) {
        rv_editor_pane_id active_pane_id = leaf.tabs[leaf.active];
        draw_pane(active_pane_id, ws.panes.panes[active_pane_id].kind, theme);
    }
    ImGui::EndChild();
    if (tabbed) {
        draw_tabs(ws, node, theme);
    }

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ws.focused_leaf = node;
    }

    ImGui::EndChild();
    ImGui::PopID();
}

void rv_editor_tile_apply(rv_editor_workspace &ws, const rv_editor_tile_action &a)
{
    if (a.what == rv_editor_tile_action::op::none) {
        return;
    }

    if (a.what == rv_editor_tile_action::op::split) {
        rv_editor_pane_id id = rv_editor_pane_add(ws.panes, rv_editor_pane_kind::empty);
        rv_editor_tile_insert(ws.layout, a.leaf, id, a.dock);
    } else if (a.what == rv_editor_tile_action::op::set_kind) {
        rv_editor_pane_set_kind(ws.panes, a.pane, a.kind);
    } else if (a.what == rv_editor_tile_action::op::maximize) {
        rv_editor_tile_toggle_maximize(ws.layout, a.leaf);
    } else if (a.what == rv_editor_tile_action::op::close) {
        rv_editor_tile_remove(ws.layout, a.pane);
    } else if (a.what == rv_editor_tile_action::op::close_leaf) {
        // A copy: removing the last pane frees the leaf the list lives in.
        const std::vector<rv_editor_pane_id> tabs = ws.layout.nodes[a.leaf].leaf.tabs;
        for (const rv_editor_pane_id pane : tabs) {
            rv_editor_tile_remove(ws.layout, pane);
        }
    }

    // Update focused_leaf if it is no longer valid.
    if (a.pane != rv_editor_tile_none) {
        if (ws.focused_leaf >= ws.layout.nodes.size() ||
            ws.layout.nodes[ws.focused_leaf].kind != rv_editor_tile_kind::leaf) {
            ws.focused_leaf = rv_editor_tile_find(ws.layout, a.pane);
        }
    } else {
        if (ws.focused_leaf >= ws.layout.nodes.size() ||
            ws.layout.nodes[ws.focused_leaf].kind != rv_editor_tile_kind::leaf) {
            ws.focused_leaf = rv_editor_tile_none;
        }
    }
}

} // namespace

void rv_editor_workspace_draw(rv_editor_workspace &ws, const rv_editor_theme &theme, rv_editor_pane_draw_fn draw_pane)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##workspace", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    const float s = theme.scale;
    const float bevel = theme.bevel_px * s;
    const float frame_h = ImGui::GetFrameHeight();
    // Leaf chrome: the frame, the header, room for a tab strip and the content padding.
    const float pad = theme.pad_px * s;
    const rv_editor_tile_metrics m{ static_cast<int>(pad),
        { static_cast<int>(2.0f * (bevel + pad)), static_cast<int>(2.0f * (bevel + frame_h + pad)) } };

    std::vector<rv_editor_size> pane_min(ws.panes.panes.size());
    const ImVec2 glyph = ImGui::CalcTextSize("M");
    for (size_t i = 0; i < pane_min.size(); ++i) {
        if (ws.panes.panes[i].kind == rv_editor_pane_kind::game) {
            pane_min[i] = {0, 0};
        } else {
            pane_min[i] = {static_cast<int>(glyph.x * 20), static_cast<int>(frame_h * 4)};
        }
    }

    const rv_editor_rect area{ static_cast<int>(viewport->WorkPos.x), static_cast<int>(viewport->WorkPos.y),
        static_cast<int>(viewport->WorkSize.x), static_cast<int>(viewport->WorkSize.y) };
    std::vector<rv_editor_tile_place> places = rv_editor_layout_place(ws.layout, area, m, pane_min);

    std::vector<rv_editor_rect> rect_of(ws.layout.nodes.size());
    for (const auto &place : places) {
        rect_of[place.node] = place.rect;
    }

    rv_editor_tile_action action;
    for (const auto &place : places) {
        const auto &node = ws.layout.nodes[place.node];
        if (node.kind == rv_editor_tile_kind::split) {
            const auto &sp = node.split;
            const auto &a = rect_of[sp.first];
            const auto &b = rect_of[sp.second];
            float fa = 0.0f;
            float fb = 0.0f;
            float length = 0.0f;
            float min_a = 0.0f;
            float min_b = 0.0f;

            if (sp.axis == rv_editor_axis::x) {
                fa = static_cast<float>(a.w);
                fb = static_cast<float>(b.w);
                length = static_cast<float>(place.rect.h);
                min_a = static_cast<float>(rv_editor_tile_min_size(ws.layout, sp.first, m, pane_min).w);
                min_b = static_cast<float>(rv_editor_tile_min_size(ws.layout, sp.second, m, pane_min).w);
                ImGui::SetCursorScreenPos(ImVec2(a.x + a.w, place.rect.y));
            } else {
                fa = static_cast<float>(a.h);
                fb = static_cast<float>(b.h);
                length = static_cast<float>(place.rect.w);
                min_a = static_cast<float>(rv_editor_tile_min_size(ws.layout, sp.first, m, pane_min).h);
                min_b = static_cast<float>(rv_editor_tile_min_size(ws.layout, sp.second, m, pane_min).h);
                ImGui::SetCursorScreenPos(ImVec2(place.rect.x, a.y + a.h));
            }

            char id[32];
            std::snprintf(id, sizeof(id), "##split%u", place.node);
            if (rv_editor_splitter(id, sp.axis, length, &fa, &fb, min_a, min_b, theme) && fa + fb > 0) {
                rv_editor_tile_set_ratio(ws.layout, place.node, fa / (fa + fb));
            }
        } else if (node.kind == rv_editor_tile_kind::leaf) {
            draw_leaf(ws, place.node, place.rect, theme, draw_pane, action);
        }
    }

    rv_editor_tile_apply(ws, action);
    ImGui::End();
}

} // namespace rv_editor
