// Help page: keyboard shortcuts filtered by user input.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "imgui.h"

#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

constexpr float filter_width_in_font_sizes = 30.0f;
constexpr int help_table_columns = 2;

// Command and keys columns: no headers, each as wide as its widest row.
constexpr rv_editor_table_column help_columns[help_table_columns] = {
    { "", rv_editor_table_width::fixed },
    { "", rv_editor_table_width::fixed },
};

// Row bands, columns fitted to their contents, as the page always drew them.
constexpr rv_editor_table_style help_table_style{ .row_bands = true, .sizing = rv_editor_table_sizing::fixed_fit };

} // namespace

void rv_editor_page_help(rv_editor_shell &shell, const rv_editor_theme &theme)
{
    rv_editor_pane_header(rv_editor_text("page_help.title"), true, theme);
    ImGui::AlignTextToFramePadding();
    rv_editor_label(rv_editor_text("page_help.filter_label"), rv_editor_label_kind::normal, theme);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * filter_width_in_font_sizes);
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
    if (!rv_editor_table_begin("##keys", help_columns, help_table_columns, help_table_style)) {
        return;
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
            rv_editor_label(group_text, rv_editor_label_kind::bright, theme);
        }
        rv_editor_table_row();
        rv_editor_table_cell();
        rv_editor_label(label_text, rv_editor_label_kind::normal, theme);
        rv_editor_table_cell();
        rv_editor_label(keys_text, rv_editor_label_kind::normal, theme);
    }
    rv_editor_table_end();
}

} // namespace rv_editor
