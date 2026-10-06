// Tiled workspace: layout, splitting, tab rows, maximize, focus.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme_imgui.hpp"
#include "ui/rv_editor_draw.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Monospace glyph for measuring cell width in layout calculations.
constexpr const char *grid_cell_width_probe = "M";

} // namespace

const char *rv_editor_pane_title(rv_editor_pane_kind kind)
{
    switch (kind) {
    case rv_editor_pane_kind::empty:
        return rv_editor_text("workspace.pane_empty");
    case rv_editor_pane_kind::catalog:
        return rv_editor_text("workspace.pane_catalog");
    case rv_editor_pane_kind::project:
        return rv_editor_text("workspace.pane_project");
    case rv_editor_pane_kind::files:
        return rv_editor_text("workspace.pane_files");
    case rv_editor_pane_kind::assets:
        return rv_editor_text("workspace.pane_assets");
    case rv_editor_pane_kind::scene:
        return rv_editor_text("workspace.pane_scene");
    case rv_editor_pane_kind::hierarchy:
        return rv_editor_text("workspace.pane_hierarchy");
    case rv_editor_pane_kind::inspector:
        return rv_editor_text("workspace.pane_inspector");
    case rv_editor_pane_kind::game:
        return rv_editor_text("workspace.pane_game");
    case rv_editor_pane_kind::code:
        return rv_editor_text("workspace.pane_code");
    case rv_editor_pane_kind::controls:
        return rv_editor_text("workspace.pane_controls");
    case rv_editor_pane_kind::run_config:
        return rv_editor_text("workspace.pane_run_config");
    case rv_editor_pane_kind::output:
        return rv_editor_text("workspace.pane_output");
    case rv_editor_pane_kind::terminal:
        return rv_editor_text("workspace.pane_terminal");
    case rv_editor_pane_kind::problems:
        return rv_editor_text("workspace.pane_problems");
    case rv_editor_pane_kind::search:
        return rv_editor_text("workspace.pane_search");
    case rv_editor_pane_kind::toolchest:
        return rv_editor_text("workspace.pane_toolchest");
    case rv_editor_pane_kind::runtime_log:
        return rv_editor_text("workspace.pane_runtime_log");
    case rv_editor_pane_kind::build_log:
        return rv_editor_text("workspace.pane_build_log");
    case rv_editor_pane_kind::observe:
        return rv_editor_text("workspace.pane_observe");
    case rv_editor_pane_kind::findings:
        return rv_editor_text("workspace.pane_findings");
    case rv_editor_pane_kind::candidate:
        return rv_editor_text("workspace.pane_candidate");
    case rv_editor_pane_kind::release_controls:
        return rv_editor_text("workspace.pane_release_controls");
    case rv_editor_pane_kind::build_result:
        return rv_editor_text("workspace.pane_build_result");
    case rv_editor_pane_kind::checks:
        return rv_editor_text("workspace.pane_checks");
    case rv_editor_pane_kind::session:
        return rv_editor_text("workspace.pane_session");
    case rv_editor_pane_kind::test_case:
        return rv_editor_text("workspace.pane_test_case");
    case rv_editor_pane_kind::open_project:
        return rv_editor_text("workspace.pane_open_project");
    case rv_editor_pane_kind::settings:
        return rv_editor_text("workspace.pane_settings");
    case rv_editor_pane_kind::help:
        return rv_editor_text("workspace.pane_help");
    case rv_editor_pane_kind::review_changes:
        return rv_editor_text("workspace.pane_review_changes");
    case rv_editor_pane_kind::manual:
        return rv_editor_text("workspace.pane_manual");
    }
    return rv_editor_text("workspace.pane_unknown");
}

namespace
{

// Outline width multiplier in scaled pixels: bevel plus outline on both sides.
constexpr float outline_mul = 2.0f;
// Chrome thickness multiplier: padding and bevel on both sides.
constexpr float chrome_mul = 2.0f;
// Minimum pane width in character widths (width of M glyph).
constexpr int pane_min_width_em = 20;
// Minimum pane height in frame heights.
constexpr int pane_min_height_frames = 4;
// Buffer size for split node ID string.
constexpr size_t split_id_buf_size = 32;

// A pane's title: the owner's for this frame, or its kind's.
const char *rv_editor_title_of(const rv_editor_workspace &ws, rv_editor_pane_id pane)
{
    const auto it = ws.titles.find(pane);
    return it != ws.titles.end() ? it->second.c_str() : rv_editor_pane_title(ws.panes.panes[pane].kind);
}

// The leaf's panes as the catalog's folder tabs; a click makes that pane the active one.
void draw_tabs(rv_editor_workspace &ws, uint32_t node, const rv_editor_theme &theme)
{
    const rv_editor_tile_leaf &leaf = ws.layout.nodes[node].leaf;
    std::vector<const char *> labels;
    for (const rv_editor_pane_id pane : leaf.tabs) {
        labels.push_back(rv_editor_title_of(ws, pane));
    }
    int active = static_cast<int>(leaf.active);
    int pressed = -1;
    if (rv_editor_tab_strip("##tabs", labels.data(), static_cast<int>(labels.size()), &active, theme, {}, &pressed)) {
        (void)rv_editor_tile_activate(ws.layout, leaf.tabs[static_cast<size_t>(active)]);
    }
    rv_editor_tile_drag_tabs(ws, node, pressed);
}

void draw_leaf(rv_editor_workspace &ws,
    uint32_t node,
    rv_editor_rect rect,
    const rv_editor_theme &theme,
    rv_editor_pane_draw_fn draw_pane,
    void *context,
    rv_editor_tile_action &action)
{
    const auto &leaf = ws.layout.nodes[node].leaf;
    const float s = theme.scale;
    // A black outline, then the raised frame inside it: the frame the tile's
    // content sits in is bevel plus outline wide.
    const float bevel = theme.bevel_px * s + s;
    // The tile is a window: a raised frame, the pane header, the padded content
    // and, when the leaf holds more than one pane, folder tabs under it.
    const ImVec2 outer_min(static_cast<float>(rect.x), static_cast<float>(rect.y));
    const ImVec2 outer_max(outer_min.x + rect.w, outer_min.y + rect.h);
    // The outline is brass around the tile that has the focus.
    ImGui::GetWindowDrawList()->AddRectFilled(outer_min,
        outer_max,
        rv_editor_col(node == ws.focused_leaf ? theme.selection : theme.dark));
    rv_editor_draw_panel(ImGui::GetWindowDrawList(),
        ImVec2(outer_min.x + s, outer_min.y + s),
        ImVec2(outer_max.x - s, outer_max.y - s),
        theme,
        theme.window,
        rv_editor_bevel::raised);

    ImGui::SetCursorScreenPos(ImVec2(outer_min.x + bevel, outer_min.y + bevel));
    ImGui::PushID(node);
    ImGui::BeginChild("##leaf",
        ImVec2(rect.w - outline_mul * bevel, rect.h - outline_mul * bevel),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const rv_editor_pane_id active = leaf.tabs.empty() ? rv_editor_tile_none : leaf.tabs[leaf.active];
    const char *title = active == rv_editor_tile_none ? rv_editor_text("workspace.pane_empty") : rv_editor_title_of(ws, active);
    // X takes the tile off the screen, M maximizes it.
    bool title_pressed = false;
    const rv_editor_header_action clicked =
        rv_editor_pane_header(title, node == ws.focused_leaf, theme, true, {}, &title_pressed);
    if (clicked == rv_editor_header_action::close) {
        action.what = rv_editor_tile_action::op::close_leaf;
        action.leaf = node;
    } else if (clicked == rv_editor_header_action::maximize) {
        action.what = rv_editor_tile_action::op::maximize;
        action.leaf = node;
    }
    rv_editor_tile_drag_header(ws, node, title_pressed);

    // The header and the tab strip: a right click there opens the tile's menu.
    const ImVec2 row_min = outer_min;
    const ImVec2 row_max(outer_max.x, ImGui::GetCursorScreenPos().y);

    // Context menu.
    if (ImGui::IsMouseHoveringRect(row_min, row_max) && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("##tile");
    }
    rv_editor_menu_style_push();
    if (ImGui::BeginPopup("##tile")) {
        if (ImGui::MenuItem(rv_editor_text("workspace.split_right"))) {
            action.what = rv_editor_tile_action::op::split;
            action.leaf = node;
            action.dock = rv_editor_tile_dock::right;
        }
        if (ImGui::MenuItem(rv_editor_text("workspace.split_down"))) {
            action.what = rv_editor_tile_action::op::split;
            action.leaf = node;
            action.dock = rv_editor_tile_dock::bottom;
        }
        if (ImGui::BeginMenu(rv_editor_text("workspace.change_to"), active != rv_editor_tile_none)) {
            for (uint32_t k = 0; k <= static_cast<uint32_t>(rv_editor_pane_kind_last); ++k) {
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
        const bool is_maximized = ws.layout.maximized_leaf == node;
        const char *maximize_label = is_maximized ? rv_editor_text("workspace.restore") : rv_editor_text("workspace.maximize");
        if (ImGui::MenuItem(maximize_label)) {
            action.what = rv_editor_tile_action::op::maximize;
            action.leaf = node;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(rv_editor_text("workspace.close"), nullptr, false, active != rv_editor_tile_none)) {
            action.what = rv_editor_tile_action::op::close;
            action.pane = active;
        }
        ImGui::EndPopup();
    }
    rv_editor_menu_style_pop();

    // The content keeps the theme's padding off the frame, like every catalog pane.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(theme.pad_px * s, theme.pad_px * s));
    const bool tabbed = leaf.tabs.size() > 1;
    const float tabs_h = tabbed ? ImGui::GetFrameHeight() : 0.0f;
    // Every pane draws straight into this window, so its focus is the pane's.
    if (ws.focus_request == node) {
        ws.focus_request = rv_editor_tile_none;
        ImGui::SetNextWindowFocus();
    }
    rv_editor_scroll_begin("##pane", ImVec2(0, -tabs_h), false, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    // A sunken well inside the raised frame: the window's double edge. Drawn by
    // the content window itself, whose background would cover the parent's lines.
    const ImVec2 well_min = ImGui::GetWindowPos();
    const ImVec2 well_max(well_min.x + ImGui::GetWindowWidth(), well_min.y + ImGui::GetWindowHeight());
    rv_editor_draw_bevel(ImGui::GetWindowDrawList(), well_min, well_max, theme, rv_editor_bevel::sunken);
    if (!leaf.tabs.empty()) {
        rv_editor_pane_id active_pane_id = leaf.tabs[leaf.active];
        draw_pane(context, active_pane_id, ws.panes.panes[active_pane_id].kind, theme);
    }
    rv_editor_scroll_end(theme);
    if (tabbed) {
        // Right under the well: EndChild leaves the cursor an item spacing lower,
        // which would push the tabs' bottom under the tile's frame.
        ImGui::SetCursorScreenPos(ImVec2(well_min.x, well_max.y));
        draw_tabs(ws, node, theme);
    }

    // The click that focuses a tile also activates the widget under it, in a child
    // window: hover must count while that widget holds the mouse.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ws.focused_leaf = node;
    }

    ImGui::EndChild();
    ImGui::PopID();
}

void rv_editor_tile_apply(rv_editor_workspace &ws,
    const rv_editor_tile_action &a,
    rv_editor_pane_close_fn close_pane,
    void *context)
{
    if (a.what == rv_editor_tile_action::op::none) {
        return;
    }
    // Every pane the change takes away is asked first; one refusal keeps them all.
    auto may_close = [&](rv_editor_pane_id pane) {
        return close_pane == nullptr || close_pane(context, pane);
    };
    if (a.what == rv_editor_tile_action::op::close && !may_close(a.pane)) {
        return;
    }
    if (a.what == rv_editor_tile_action::op::set_kind && ws.panes.panes[a.pane].kind != a.kind && !may_close(a.pane)) {
        return;
    }
    if (a.what == rv_editor_tile_action::op::close_leaf) {
        for (const rv_editor_pane_id pane : ws.layout.nodes[a.leaf].leaf.tabs) {
            if (!may_close(pane)) {
                return;
            }
        }
    }

    if (a.what == rv_editor_tile_action::op::split) {
        // A code tile splits into another code tile, a second nvim window on its
        // file; any other tile into an empty one to choose a kind for.
        const rv_editor_tile_leaf &from = ws.layout.nodes[a.leaf].leaf;
        const bool code = !from.tabs.empty() && ws.panes.panes[from.tabs[from.active]].kind == rv_editor_pane_kind::code;
        rv_editor_pane_id id = rv_editor_pane_add(ws.panes, code ? rv_editor_pane_kind::code : rv_editor_pane_kind::empty);
        rv_editor_tile_insert(ws.layout, a.leaf, id, a.dock);
    } else if (a.what == rv_editor_tile_action::op::set_kind) {
        (void)rv_editor_pane_set_kind(ws.panes, a.pane, a.kind);
    } else if (a.what == rv_editor_tile_action::op::maximize) {
        (void)rv_editor_tile_toggle_maximize(ws.layout, a.leaf);
    } else if (a.what == rv_editor_tile_action::op::close) {
        (void)rv_editor_tile_remove(ws.layout, a.pane);
    } else if (a.what == rv_editor_tile_action::op::close_leaf) {
        // A copy: removing the last pane frees the leaf the list lives in.
        const std::vector<rv_editor_pane_id> tabs = ws.layout.nodes[a.leaf].leaf.tabs;
        for (const rv_editor_pane_id pane : tabs) {
            (void)rv_editor_tile_remove(ws.layout, pane);
        }
    }

    // Update focused_leaf if it is no longer valid.
    if (a.pane != rv_editor_tile_none) {
        if (ws.focused_leaf >= ws.layout.nodes.size() || ws.layout.nodes[ws.focused_leaf].kind != rv_editor_tile_kind::leaf) {
            ws.focused_leaf = rv_editor_tile_find(ws.layout, a.pane);
        }
    } else {
        if (ws.focused_leaf >= ws.layout.nodes.size() || ws.layout.nodes[ws.focused_leaf].kind != rv_editor_tile_kind::leaf) {
            ws.focused_leaf = rv_editor_tile_none;
        }
    }
}

} // namespace

void rv_editor_workspace_draw(rv_editor_workspace &ws,
    const rv_editor_theme &theme,
    rv_editor_pane_draw_fn draw_pane,
    rv_editor_pane_close_fn close_pane,
    void *context,
    rv_editor_rect area)
{
    // A child of the current window, so a workspace can sit inside any pane: the
    // Widget Catalog shows one.
    ImGui::SetCursorScreenPos(ImVec2(static_cast<float>(area.x), static_cast<float>(area.y)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    // Scrollbars appear only when the window is smaller than the tree's minimum.
    rv_editor_scroll_begin("##workspace", ImVec2(static_cast<float>(area.w), static_cast<float>(area.h)), true);
    ImGui::PopStyleVar();

    const float s = theme.scale;
    const float bevel = theme.bevel_px * s + s; // with the tile's outline
    const float frame_h = ImGui::GetFrameHeight();
    // Leaf chrome: the frame, the header, room for a tab strip and the content padding.
    const float pad = theme.pad_px * s;
    const rv_editor_tile_metrics m{ static_cast<int>(pad),
        { static_cast<int>(chrome_mul * (bevel + pad)),
            static_cast<int>(chrome_mul * (bevel + pad) + frame_h + ImGui::GetStyle().ItemSpacing.y) },
        static_cast<int>(frame_h) };

    std::vector<rv_editor_size> pane_min(ws.panes.panes.size());
    const ImVec2 glyph = ImGui::CalcTextSize(grid_cell_width_probe);
    for (size_t i = 0; i < pane_min.size(); ++i) {
        // What the owner measured: the Game's frame at 1x once the console has
        // sent one, a strip of controls as tall as its rows.
        const auto it = ws.minimums.find(static_cast<rv_editor_pane_id>(i));
        if (it != ws.minimums.end()) {
            pane_min[i] = it->second;
        } else if (ws.panes.panes[i].kind == rv_editor_pane_kind::game) {
            // Smaller until the first frame, and Fit shows what fits.
            pane_min[i] = { 0, 0 };
        } else {
            pane_min[i] = { static_cast<int>(glyph.x * pane_min_width_em), static_cast<int>(frame_h * pane_min_height_frames) };
        }
    }

    // A window smaller than the tree's minimum scrolls instead of squeezing tiles
    // below their minimums, so every control stays reachable.
    const uint32_t shown = ws.layout.maximized_leaf < ws.layout.nodes.size() ? ws.layout.maximized_leaf : ws.layout.root;
    const rv_editor_size need = rv_editor_tile_min_size(ws.layout, shown, m, pane_min);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const rv_editor_rect placed{ static_cast<int>(origin.x),
        static_cast<int>(origin.y),
        std::max(static_cast<int>(avail.x), need.w),
        std::max(static_cast<int>(avail.y), need.h) };
    std::vector<rv_editor_tile_place> places = rv_editor_layout_place(ws.layout, placed, m, pane_min);

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

            char id[split_id_buf_size];
            std::snprintf(id, sizeof(id), "##split%u", place.node);
            if (rv_editor_splitter(id, sp.axis, length, &fa, &fb, min_a, min_b, theme) && fa + fb > 0) {
                (void)rv_editor_tile_set_ratio(ws.layout, place.node, fa / (fa + fb));
                ws.dragged = true;
            }
        } else if (node.kind == rv_editor_tile_kind::leaf) {
            draw_leaf(ws, place.node, place.rect, theme, draw_pane, context, action);
        }
    }

    ws.rects = rect_of;
    rv_editor_tile_drag_update(ws, rect_of, theme);

    // The content size the scrollbars measure.
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(static_cast<float>(placed.w), static_cast<float>(placed.h)));

    // A pane's request waits for a frame the menu left empty.
    if (action.what == rv_editor_tile_action::op::none && ws.pending.what != rv_editor_tile_action::op::none) {
        action = ws.pending;
    }
    ws.pending = rv_editor_tile_action();

    rv_editor_tile_apply(ws, action, close_pane, context);
    rv_editor_scroll_end(theme);
}

} // namespace rv_editor
