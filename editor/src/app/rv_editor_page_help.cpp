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
    (void)rv_editor_tiled_area_begin("##keys_area", rv_editor_help_content_width(), theme);
    if (rv_editor_table_begin("##keys", help_columns, help_table_columns, help_table_style)) {
        std::string_view last_group;
        const auto draw_row = [&](const char *group_id, const char *label_id, const char *keys) {
            const char *label = rv_editor_text(label_id);
            if (!holds(label) && !holds(keys)) {
                return;
            }
            const char *group = rv_editor_text(group_id);
            if (last_group != group) {
                last_group = group;
                rv_editor_table_row();
                rv_editor_table_cell();
                rv_editor_text_item(group, theme.text_bright);
            }
            rv_editor_table_row();
            rv_editor_table_cell();
            rv_editor_text_item(label);
            rv_editor_table_cell();
            rv_editor_text_item(keys);
        };
        for (size_t i = 0; i < rv_editor_shortcut_specs.size(); ++i) {
            const auto &spec = rv_editor_shortcut_specs[i];
            draw_row(spec.group_id,
                spec.label_id,
                rv_editor_shell_shortcut_label(shell, static_cast<rv_editor_shortcut_action>(i)));
        }
        draw_row("workspace.pane_game", "page_help.label_release_input", rv_editor_text("page_help.shortcut_game"));
        draw_row("workspace.pane_code", "shell_menu.save", rv_editor_text("shell_menu.shortcut_save"));
        draw_row("workspace.pane_code", "page_help.label_undo", rv_editor_text("page_help.shortcut_undo"));
        draw_row("workspace.pane_code", "page_help.label_vim", rv_editor_text("page_help.shortcut_vim"));
        if (last_group.empty()) {
            rv_editor_table_row();
            rv_editor_table_cell();
            rv_editor_content_empty_row(rv_editor_text("page_help.no_match"), theme);
        }
        rv_editor_table_end();
    }
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
