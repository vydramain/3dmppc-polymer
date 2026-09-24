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
    }
    return "?";
}

namespace
{

void draw_leaf(rv_editor_workspace &ws, uint32_t node, rv_editor_rect rect, const rv_editor_theme &theme,
    rv_editor_pane_draw_fn draw_pane)
{
    const auto &leaf = ws.layout.nodes[node].leaf;
    const float s = theme.scale;
    const float bevel = theme.bevel_px * s;
    const float frame_h = ImGui::GetFrameHeight();

    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
    ImGui::PushID(node);
    ImGui::BeginChild("##leaf", ImVec2(rect.w, rect.h), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (leaf.tabs.empty()) {
        rv_editor_pane_header("Empty", node == ws.focused_leaf, theme);
    } else if (leaf.tabs.size() == 1) {
        const char *title = rv_editor_pane_title(ws.panes.panes[leaf.tabs[0]].kind);
        rv_editor_pane_header(title, node == ws.focused_leaf, theme);
    } else {
        for (size_t i = 0; i < leaf.tabs.size(); ++i) {
            if (i > 0) {
                ImGui::SameLine(0, 0);
            }
            const rv_editor_pane_id pane_id = leaf.tabs[i];
            const char *title = rv_editor_pane_title(ws.panes.panes[pane_id].kind);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s##%zu", title, i);
            const ImVec2 size(ImGui::CalcTextSize(title).x + 2.0f * theme.pad_px * s, frame_h);
            if (ImGui::Selectable(buf, i == leaf.active, 0, size)) {
                rv_editor_tile_activate(ws.layout, pane_id);
            }
        }
    }

    // A double click on the header or the tab row toggles maximize.
    const ImVec2 row_min(rect.x, rect.y);
    const ImVec2 row_max(rect.x + rect.w, rect.y + frame_h + 2.0f * bevel);
    if (ImGui::IsMouseHoveringRect(row_min, row_max) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        rv_editor_tile_toggle_maximize(ws.layout, node);
    }

    ImGui::BeginChild("##pane", ImVec2(0, 0), ImGuiChildFlags_None);
    if (!leaf.tabs.empty()) {
        rv_editor_pane_id active_pane_id = leaf.tabs[leaf.active];
        draw_pane(active_pane_id, ws.panes.panes[active_pane_id].kind, theme);
    }
    ImGui::EndChild();

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ws.focused_leaf = node;
    }

    ImGui::EndChild();
    ImGui::PopID();
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
    const rv_editor_tile_metrics m{ static_cast<int>(theme.pad_px * s),
        { static_cast<int>(2.0f * bevel), static_cast<int>(frame_h + 2.0f * bevel) } };

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
            draw_leaf(ws, place.node, place.rect, theme, draw_pane);
        }
    }

    ImGui::End();
}

} // namespace rv_editor
