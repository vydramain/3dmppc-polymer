// Help page: keyboard shortcuts filtered by user input.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "imgui.h"

#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"

namespace rv_editor
{

namespace
{

constexpr float filter_width_in_font_sizes = 30.0f;
// The filter field is at least this wide, and the content area under it, in font sizes.
constexpr float filter_min_em = 12.0f;
constexpr float area_min_em = 20.0f;
constexpr int help_table_columns = 2;

// Command and keys columns: no headers, each as wide as its widest row.
constexpr rv_editor_table_column help_columns[help_table_columns] = {
    { "", rv_editor_table_width::fixed },
    { "", rv_editor_table_width::fixed },
};

// Row bands, columns fitted to their contents, as the page always drew them.
constexpr rv_editor_table_style help_table_style{ .row_bands = true, .sizing = rv_editor_table_sizing::fixed_fit };

float rv_editor_help_content_width()
{
    const float row = ImGui::CalcTextSize(rv_editor_text("page_help.filter_label")).x + ImGui::GetStyle().ItemSpacing.x +
        ImGui::GetFontSize() * filter_min_em;
    return std::max(row, ImGui::GetFontSize() * area_min_em);
}

} // namespace

rv_editor_size rv_editor_help_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_help_content_width(), ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y);
}

void rv_editor_page_help(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    ImGui::AlignTextToFramePadding();
    rv_editor_label(rv_editor_text("page_help.filter_label"),
        rv_editor_label_kind::normal,
        theme,
        rv_editor_label_layout::line);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize() * filter_width_in_font_sizes, ImGui::GetContentRegionAvail().x));
    rv_editor_text_field("##filter", shell.help_filter, sizeof(shell.help_filter), theme);
    // A row stays when its command or its keys hold the filter, case aside.
    const auto holds = [&](const char *text) {
        const std::string_view f = shell.help_filter;
        const std::string_view t = text;
        return std::search(t.begin(), t.end(), f.begin(), f.end(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        }) != t.end();
    };
    // Grouped by the menu that holds each command, in menu-bar order; then by
    // where a shortcut with no menu works. Within a group, the menu's own order.
    struct rv_editor_help_row {
        const char *group_id;
        const char *label_id;
        const char *keys_id;
    };
    constexpr rv_editor_help_row rows[] = {
        { "catalog_status.menu_file", "page_help.label_save", "page_help.shortcut_save" },
        { "shell_menu.edit", "page_help.label_undo", "page_help.shortcut_undo" },
        { "shell_menu.edit", "shell_menu.find_in_project", "shell_menu.shortcut_find" },
        { "shell_menu.project", "widgets_status.button_build", "widgets_status.shortcut_build" },
        { "catalog_status.menu_run", "page_help.label_run", "widgets_status.shortcut_run" },
        { "catalog_status.menu_run", "widgets_status.button_pause", "widgets_status.shortcut_pause" },
        { "catalog_status.menu_run", "widgets_status.button_step_frame", "widgets_status.shortcut_step" },
        { "catalog_status.menu_run", "widgets_status.button_stop", "widgets_status.shortcut_stop" },
        { "catalog_status.menu_run", "widgets_status.button_reload", "widgets_status.shortcut_reload" },
        { "shell_menu.window", "page_help.label_focus", "page_help.shortcut_focus" },
        { "workspace.pane_game", "page_help.label_release_input", "page_help.shortcut_game" },
        { "workspace.pane_code", "page_help.label_vim", "page_help.shortcut_vim" },
        { "shell_menu.help", "manual.start_label", "page_help.shortcut_f1" },
    };
    bool any = false;
    for (const auto &r : rows) {
        any = any || holds(rv_editor_text(r.label_id)) || holds(rv_editor_text(r.keys_id));
    }
    (void)rv_editor_tiled_area_begin("##keys_area", rv_editor_help_content_width(), theme);
    if (rv_editor_table_begin("##keys", help_columns, help_table_columns, help_table_style)) {
        // No match: the area stays, with a line that says so.
        if (!any) {
            rv_editor_table_row();
            rv_editor_table_cell();
            rv_editor_content_empty_row(rv_editor_text("page_help.no_match"), theme);
        }
        const char *last_group_text = nullptr;
        for (const auto &r : rows) {
            const char *label_text = rv_editor_text(r.label_id);
            const char *keys_text = rv_editor_text(r.keys_id);
            if (!holds(label_text) && !holds(keys_text)) {
                continue;
            }
            const char *group_text = rv_editor_text(r.group_id);
            if (last_group_text == nullptr || std::string_view(last_group_text) != group_text) {
                last_group_text = group_text;
                rv_editor_table_row();
                rv_editor_table_cell();
                rv_editor_text_item(group_text, theme.text_bright);
            }
            rv_editor_table_row();
            rv_editor_table_cell();
            rv_editor_text_item(label_text);
            rv_editor_table_cell();
            rv_editor_text_item(keys_text);
        }
        rv_editor_table_end();
    }
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
