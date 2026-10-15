// Search Results: the query, where it looked, and each file:line holding it, opening
// in a code tile. Run on Enter or Find, never while typing.

#include "panes/rv_editor_panes.hpp"

#include <string>
#include <system_error>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Number of columns in search results table: file, line number, text excerpt.
constexpr int search_table_columns = 3;

// Query input field width in font size units.
constexpr float query_field_width_em = 14.0f;

void rv_editor_search_list(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_search_result &r = app.project_search.result;
    const rv_editor_table_column columns[] = {
        { rv_editor_text("pane_search.table_file"), rv_editor_table_width::fixed },
        { rv_editor_text("pane_search.table_line"), rv_editor_table_width::fixed },
        { rv_editor_text("pane_search.table_text") },
    };
    if (!rv_editor_table_begin("##hits", columns, search_table_columns, { .row_bands = true, .header = true })) {
        return;
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
            const bool open = row.double_clicked || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter));
            if (open) {
                app.open_requests.push_back({ hit.file, hit.line });
            }
            rv_editor_table_cell();
            const std::string position = std::to_string(hit.line) + ":" + std::to_string(hit.column);
            rv_editor_label(position.c_str(), rv_editor_label_kind::normal, theme);
            rv_editor_table_cell();
            rv_editor_label(hit.excerpt.c_str(), rv_editor_label_kind::normal, theme);
            ImGui::PopID();
        }
    }
    rv_editor_table_end();
}

} // namespace

void rv_editor_pane_search(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    rv_editor_search_view &s = app.project_search;
    rv_editor_shelf_begin("##shelf", theme);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * query_field_width_em);
    if (s.focus) {
        ImGui::SetKeyboardFocusHere();
        s.focus = false;
    }
    rv_editor_text_field("##query", s.query, sizeof(s.query), theme);
    rv_editor_item_tooltip(rv_editor_text("pane_search.query_tooltip"));
    // A single-line field lets go of the keyboard on Enter.
    bool run = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    rv_editor_flow(rv_editor_button_width(rv_editor_text("pane_search.find_button")));
    const char *why_not = s.query[0] == '\0' ? rv_editor_text("pane_search.type_first") : nullptr;
    run |= rv_editor_button(rv_editor_text("pane_search.find_button"), theme, { rv_editor_look::live, why_not });
    rv_editor_flow(rv_editor_checkbox_width(rv_editor_text("pane_search.match_case")));
    rv_editor_checkbox(rv_editor_text("pane_search.match_case"), &s.match_case, theme);
    rv_editor_flow(rv_editor_checkbox_width(rv_editor_text("pane_search.skipped_folders")));
    rv_editor_checkbox(rv_editor_text("pane_search.skipped_folders"), &s.all, theme);
    const std::string also_search_tooltip =
        rv_editor_text_format("pane_search.also_search", std::make_format_args(rv_editor_search_skipped));
    rv_editor_item_tooltip(also_search_tooltip.c_str());
    rv_editor_shelf_end();
    if (run && s.query[0] != '\0') {
        // Runs on the UI thread; a worker thread when projects grow past a blink.
        s.result = rv_editor_search_run(app.project.root, s.query, s.match_case, s.all);
        s.searched = s.query;
        s.searched_all = s.all;
    }

    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const auto body = [&]() {
        if (s.searched.empty()) {
            const std::string nothing_searched_msg =
                rv_editor_text_format("pane_search.nothing_searched", std::make_format_args(rv_editor_search_skipped));
            rv_editor_label(nothing_searched_msg.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
            return;
        }
        const rv_editor_search_result &r = s.result;
        // The scope stated with the result, so an empty one says where it did not look.
        const std::string project_name = app.project.root.filename().string();
        std::string scope =
            rv_editor_text_format("pane_search.scope_base", std::make_format_args(s.searched, r.files, project_name));
        if (r.skipped_dirs != 0) {
            const std::string skipped_msg = rv_editor_text_format("pane_search.scope_skipped_dirs",
                std::make_format_args(r.skipped_dirs, rv_editor_search_skipped));
            scope += skipped_msg;
        }
        if (r.skipped_files != 0) {
            const std::string files_msg =
                rv_editor_text_format("pane_search.scope_skipped_files", std::make_format_args(r.skipped_files));
            scope += files_msg;
        }
        const std::string matches_text =
            std::to_string(r.hits.size()) + (r.truncated ? "+" : "") + " " + rv_editor_text("pane_search.matches");
        const std::string count = r.hits.empty() ? rv_editor_text("pane_search.no_match") : matches_text;
        rv_editor_status(count.c_str(), r.hits.empty() ? rv_editor_status_kind::warning : rv_editor_status_kind::ok, theme);
        ImGui::SameLine();
        rv_editor_label(scope.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
        if (r.truncated) {
            const size_t hits_count = r.hits.size();
            const std::string stopped_msg = rv_editor_text_format("pane_search.stopped_at", std::make_format_args(hits_count));
            rv_editor_label(stopped_msg.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
        }
        if (r.hits.empty()) {
            const bool show_limited = r.skipped_dirs != 0 && !s.searched_all;
            const char *msg = rv_editor_text(show_limited ? "pane_search.no_match_limited" : "pane_search.no_match_full");
            rv_editor_label(msg, rv_editor_label_kind::normal, theme, rv_editor_label_layout::wrap);
            return;
        }
        rv_editor_search_list(app, theme);
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
