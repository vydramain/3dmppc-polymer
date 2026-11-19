// Search Results: the query, where it looked, and each file:line holding it, opening
// in a code tile. Run on Enter or Find, never while typing.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <string>
#include <system_error>

#include "imgui.h"

#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

namespace
{

// Number of columns in search results table: file, line number, text excerpt.
constexpr int search_table_columns = 3;

// Query input field width in font size units: at most, and at the least the shelf's row keeps.
constexpr float query_field_width_em = 14.0f;
constexpr float query_field_min_width_em = 8.0f;

// The shelf's row without the query field: Find, Match Case and Skipped Folders Too, each with the gap before it.
float rv_editor_search_buttons_width()
{
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    return rv_editor_button_width(rv_editor_text("pane_search.find_button")) +
        rv_editor_checkbox_width(rv_editor_text("pane_search.match_case")) +
        rv_editor_checkbox_width(rv_editor_text("pane_search.skipped_folders")) + 3.0f * gap;
}

// The widest row: the shelf's with the query at its minimum, or the message box's with its button.
float rv_editor_search_content_width()
{
    return std::max(
        rv_editor_tiled_shelf_width(ImGui::GetFontSize() * query_field_min_width_em + rv_editor_search_buttons_width()),
        rv_editor_message_box_min_width({ { rv_editor_text("panes.open_project_box") } }));
}

void rv_editor_search_list(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_search_result &r = app.project_search.result;
    const rv_editor_table_column columns[] = {
        { rv_editor_text("pane_search.table_file") },
        { rv_editor_text("pane_search.table_line") },
        { rv_editor_text("pane_search.table_text") },
    };
    if (!rv_editor_table_begin("##hits",
            columns,
            search_table_columns,
            { .row_bands = true, .header = true, .sizing = rv_editor_table_sizing::fixed_fit })) {
        return;
    }
    if (r.hits.empty()) {
        rv_editor_table_row();
        rv_editor_table_cell();
        rv_editor_content_empty_row(rv_editor_text("pane_search.empty_row"), theme);
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(r.hits.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const rv_editor_search_hit &hit = r.hits[static_cast<size_t>(i)];
            ImGui::PushID(i);
            rv_editor_table_row();
            rv_editor_table_cell();
            std::error_code ec;
            const std::filesystem::path shown = std::filesystem::relative(hit.file, app.project.root, ec);
            const std::string label = (ec || shown.empty() ? hit.file : shown).string();
            // The whole row answers a click; double click or Enter opens the place.
            const rv_editor_list_result row = rv_editor_list_row("##hit", label.c_str(), theme, { .span_columns = true });
            if (ImGui::IsItemHovered()) {
                rv_editor_tooltip(hit.file.string().c_str());
            }
            const bool open = row.double_clicked || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter));
            if (open) {
                app.open_requests.push_back({ hit.file, hit.line });
            }
            rv_editor_table_cell();
            const std::string position = std::to_string(hit.line) + ":" + std::to_string(hit.column);
            rv_editor_label(position.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
            rv_editor_table_cell();
            rv_editor_table_cell_text(hit.excerpt.c_str(), theme);
            ImGui::PopID();
        }
    }
    rv_editor_table_end();
}

// What the search says in the message box and how: "No project is open." with its button, the note of the last search, or
// nothing searched yet.
void rv_editor_search_say(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_search_view &s = app.project_search;
    if (!app.project.open) {
        if (rv_editor_message_box("##note",
                rv_editor_text("panes.no_project_open_message"),
                rv_editor_label_kind::normal,
                { rv_editor_text("panes.open_project_box") },
                theme,
                rv_editor_search_content_width()) == 0) {
            app.open_folder_request = true;
        }
        return;
    }
    std::string note;
    if (s.searched.empty()) {
        note = rv_editor_text_format("pane_search.nothing_searched", std::make_format_args(rv_editor_search_skipped));
        (void)rv_editor_message_box("##note", note, rv_editor_label_kind::normal, {}, theme, rv_editor_search_content_width());
        return;
    }
    const rv_editor_search_result &r = s.result;
    // The scope stated with the result, so an empty one says where it did not look.
    const std::string project_name = app.project.root.filename().string();
    note = rv_editor_text_format("pane_search.scope_base", std::make_format_args(s.searched, r.files, project_name));
    if (r.skipped_dirs != 0) {
        note += rv_editor_text_format("pane_search.scope_skipped_dirs",
            std::make_format_args(r.skipped_dirs, rv_editor_search_skipped));
    }
    if (r.skipped_files != 0) {
        note += rv_editor_text_format("pane_search.scope_skipped_files", std::make_format_args(r.skipped_files));
    }
    const std::string matches_text =
        std::to_string(r.hits.size()) + (r.truncated ? "+" : "") + " " + rv_editor_text("pane_search.matches");
    note = (r.hits.empty() ? std::string(rv_editor_text("pane_search.no_match")) : matches_text) + "  " + note;
    if (r.truncated) {
        const size_t hits_count = r.hits.size();
        note += "  " + rv_editor_text_format("pane_search.stopped_at", std::make_format_args(hits_count));
    }
    if (r.hits.empty()) {
        const bool show_limited = r.skipped_dirs != 0 && !s.searched_all;
        note += std::string("  ") + rv_editor_text(show_limited ? "pane_search.no_match_limited" : "pane_search.no_match_full");
    }
    const auto note_kind = r.hits.empty() ? rv_editor_label_kind::warning : rv_editor_label_kind::normal;
    (void)rv_editor_message_box("##note", note, note_kind, {}, theme, rv_editor_search_content_width());
}

} // namespace

rv_editor_size rv_editor_search_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_search_content_width(),
        rv_editor_tiled_shelf_block() + rv_editor_tiled_box_block());
}

void rv_editor_pane_search(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_search_view &s = app.project_search;
    rv_editor_shelf_begin("##shelf", theme);
    const float query_w = std::clamp(ImGui::GetContentRegionAvail().x - rv_editor_search_buttons_width(),
        ImGui::GetFontSize() * query_field_min_width_em,
        ImGui::GetFontSize() * query_field_width_em);
    ImGui::SetNextItemWidth(query_w);
    if (s.focus) {
        ImGui::SetKeyboardFocusHere();
        s.focus = false;
    }
    rv_editor_text_field("##query", s.query, sizeof(s.query), theme);
    rv_editor_item_tooltip(rv_editor_text("pane_search.query_tooltip"));
    // A single-line field lets go of the keyboard on Enter.
    bool run = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    ImGui::SameLine();
    const char *why_not = !app.project.open ? rv_editor_text("panes.no_project_open_message") :
        s.query[0] == '\0'                  ? rv_editor_text("pane_search.type_first") :
                                              nullptr;
    run |= rv_editor_button(rv_editor_text("pane_search.find_button"), theme, { rv_editor_look::live, why_not });
    ImGui::SameLine();
    rv_editor_checkbox(rv_editor_text("pane_search.match_case"), &s.match_case, theme);
    ImGui::SameLine();
    rv_editor_checkbox(rv_editor_text("pane_search.skipped_folders"), &s.all, theme);
    const std::string also_search_tooltip =
        rv_editor_text_format("pane_search.also_search", std::make_format_args(rv_editor_search_skipped));
    rv_editor_item_tooltip(also_search_tooltip.c_str());
    rv_editor_shelf_end();
    if (run && s.query[0] != '\0' && app.project.open) {
        // Runs on the UI thread; a worker thread when projects grow past a blink.
        s.result = rv_editor_search_run(app.project.root, s.query, s.match_case, s.all);
        s.searched = s.query;
        s.searched_all = s.all;
    }

    rv_editor_search_say(app, theme);
    (void)rv_editor_tiled_area_begin("##hits_area", rv_editor_search_content_width(), theme);
    rv_editor_search_list(app, theme);
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
