// Catalog sections the templates do not cover: the tree and the list in every state, scrolling both ways, the log.

#include "catalog/rv_editor_catalog.hpp"

#include <format>

#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Catalog list and view height in frame heights (GetFrameHeight() multiplier).
constexpr float view_height_frames = 6.0f;
constexpr float scroll_height_frames = 4.0f;
constexpr int overflow_sample_rows = 12;

// Number of text lines to display in the log area, and the multiplier for the log's vertical padding.
constexpr float log_display_rows = 4.0f;
constexpr float log_padding_y_multiplier = 2.0f;

// The tree, the list side by side from this width in font sizes; narrower they stack.
constexpr float lists_wide_em = 20.0f;

// A leaf row whose label names its state (a key of catalog_panes).
void leaf(const char *id,
    const char *key,
    const rv_editor_theme &t,
    const rv_editor_tree_style &style = {},
    const rv_editor_state &state = {})
{
    rv_editor_tree_style row = style;
    row.leaf = true;
    row.no_push = true;
    rv_editor_tree_row(id, rv_editor_text(key), t, row, state);
}

// One row per state under an open root with lines; the frozen looks come last.
void tree(const rv_editor_theme &t)
{
    rv_editor_tree_style root;
    root.default_open = true;
    root.lines = true;
    if (!rv_editor_tree_row("##root", rv_editor_text("catalog_panes.tree_my_game"), t, root).open) {
        return;
    }
    rv_editor_tree_style selected;
    selected.selected = true;
    rv_editor_tree_style dim;
    dim.dim = true;
    rv_editor_tree_style open_branch;
    open_branch.default_open = true;
    leaf("##leaf", "catalog_panes.tree_leaf", t);
    leaf("##selected", "catalog_panes.tree_selected", t, selected);
    leaf("##dim", "catalog_panes.tree_dim", t, dim);
    leaf("##disabled",
        "catalog_panes.tree_disabled",
        t,
        {},
        { rv_editor_look::normal, rv_editor_text("catalog_panes.tree_disabled_reason") });
    leaf("##hovered", "catalog_panes.tree_hovered", t, {}, { rv_editor_look::hovered });
    leaf("##pressed", "catalog_panes.tree_pressed", t, {}, { rv_editor_look::pressed });
    leaf("##focused", "catalog_panes.tree_focused", t, {}, { rv_editor_look::focused });
    if (rv_editor_tree_row("##open", rv_editor_text("catalog_panes.tree_open_branch"), t, open_branch).open) {
        leaf("##child", "catalog_panes.tree_child", t);
        rv_editor_tree_pop();
    }
    if (rv_editor_tree_row("##closed", rv_editor_text("catalog_panes.tree_closed_branch"), t).open) {
        leaf("##closed_child", "catalog_panes.tree_child", t);
        rv_editor_tree_pop();
    }
    rv_editor_tree_pop();
}

// One row per state, the frozen looks last; the live rows follow the pointer.
void list(float height, const rv_editor_theme &t)
{
    rv_editor_list_style selected;
    selected.selected = true;
    rv_editor_list_style dim;
    dim.dim = true;
    if (rv_editor_list_begin("##list", ImVec2(0.0f, height), t)) {
        rv_editor_list_row("##normal", rv_editor_text("catalog_panes.list_normal"), t);
        rv_editor_list_row("##selected", rv_editor_text("catalog_panes.list_selected"), t, selected);
        rv_editor_list_row("##dim", rv_editor_text("catalog_panes.list_dim"), t, dim);
        rv_editor_list_row("##disabled",
            rv_editor_text("catalog_panes.list_disabled"),
            t,
            {},
            { rv_editor_look::normal, rv_editor_text("catalog_panes.list_disabled_reason") });
        rv_editor_list_row("##hovered", rv_editor_text("catalog_panes.list_hovered"), t, {}, { rv_editor_look::hovered });
        rv_editor_list_row("##pressed", rv_editor_text("catalog_panes.list_pressed"), t, {}, { rv_editor_look::pressed });
        rv_editor_list_row("##focused", rv_editor_text("catalog_panes.list_focused"), t, {}, { rv_editor_look::focused });
    }
    rv_editor_list_end();
}

} // namespace

void rv_editor_catalog_tree_list(const rv_editor_theme &theme)
{
    const float height = ImGui::GetFrameHeight() * view_height_frames;
    if (ImGui::BeginTable("##panes", ImGui::GetContentRegionAvail().x >= lists_wide_em * ImGui::GetFontSize() ? 2 : 1)) {
        ImGui::TableNextColumn();
        ImGui::BeginChild("##tree", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
        tree(theme);
        ImGui::EndChild();
        ImGui::TableNextColumn();
        list(height, theme);
        ImGui::EndTable();
    }
}

void rv_editor_catalog_scroll(const rv_editor_theme &theme)
{
    // A scrolled region with the editor's own bars, both ways.
    rv_editor_scroll_begin("##cat_scroll",
        ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * scroll_height_frames),
        true,
        ImGuiChildFlags_Borders);
    for (int i = 1; i <= overflow_sample_rows; ++i) {
        const auto args = std::make_format_args(i, i, i, i, i);
        const auto text = rv_editor_text_format("catalog_more.overflow_long_line", args);
        ImGui::Text("%s", text.c_str());
    }
    rv_editor_scroll_end(theme);
}

void rv_editor_catalog_log(const rv_editor_theme &t)
{
    const float rows =
        log_display_rows * ImGui::GetTextLineHeightWithSpacing() + log_padding_y_multiplier * ImGui::GetStyle().WindowPadding.y;
    rv_editor_log_begin("##log", ImVec2(0.0f, rows), t);
    rv_editor_log_row(rv_editor_text("catalog_status.log_time_1"),
        rv_editor_text("catalog_status.log_source_burn"),
        rv_editor_severity::info,
        rv_editor_text("catalog_status.log_msg_build"),
        t);
    rv_editor_log_row(rv_editor_text("catalog_status.log_time_2"),
        rv_editor_text("catalog_status.log_source_mppc"),
        rv_editor_severity::info,
        rv_editor_text("catalog_status.log_msg_runtime"),
        t);
    rv_editor_log_row(rv_editor_text("catalog_status.log_time_3"),
        rv_editor_text("catalog_status.log_source_disc"),
        rv_editor_severity::warning,
        rv_editor_text("catalog_status.log_msg_missing_texture"),
        t);
    rv_editor_log_row(rv_editor_text("catalog_status.log_time_4"),
        rv_editor_text("catalog_status.log_source_mppc"),
        rv_editor_severity::error,
        rv_editor_text("catalog_status.log_msg_error"),
        t);
    rv_editor_log_end(t);
}

} // namespace rv_editor
