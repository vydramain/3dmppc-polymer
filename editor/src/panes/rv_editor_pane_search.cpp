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

void rv_editor_search_list(rv_editor_app &app)
{
    const rv_editor_search_result &r = app.project_search.result;
    if (!ImGui::BeginTable("##hits", search_table_columns,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        return;
    }
    ImGui::TableSetupColumn(rv_editor_text("pane_search.table_file"), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(rv_editor_text("pane_search.table_line"), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(rv_editor_text("pane_search.table_text"));
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(r.hits.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const rv_editor_search_hit &hit = r.hits[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::error_code ec;
            const std::filesystem::path shown = std::filesystem::relative(hit.file, app.project.root, ec);
            // The whole row answers a click; double click or Enter opens the place.
            const bool pressed = ImGui::Selectable((ec || shown.empty() ? hit.file : shown).c_str(), false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
            const bool open = (pressed && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ||
                (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter));
            if (open) {
                app.open_requests.push_back({ hit.file, hit.line });
            }
            ImGui::TableNextColumn();
            ImGui::Text("%d:%d", hit.line, hit.column);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(hit.excerpt.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
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
    rv_editor_flow(ImGui::GetFontSize() * query_field_width_em);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * query_field_width_em);
    if (s.focus) {
        ImGui::SetKeyboardFocusHere();
        s.focus = false;
    }
    rv_editor_text_field("##query", s.query, sizeof(s.query), theme);
    ImGui::SetItemTooltip("%s", rv_editor_text("pane_search.query_tooltip"));
    // A single-line field lets go of the keyboard on Enter.
    bool run = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    rv_editor_flow(rv_editor_button_width(rv_editor_text("pane_search.find_button")));
    const char *why_not = s.query[0] == '\0' ? rv_editor_text("pane_search.type_first") : nullptr;
    run |= rv_editor_button(rv_editor_text("pane_search.find_button"), theme, { rv_editor_look::live, why_not });
    rv_editor_flow(rv_editor_checkbox_width(rv_editor_text("pane_search.match_case")));
    rv_editor_checkbox(rv_editor_text("pane_search.match_case"), &s.match_case, theme);
    rv_editor_flow(rv_editor_checkbox_width(rv_editor_text("pane_search.skipped_folders")));
    rv_editor_checkbox(rv_editor_text("pane_search.skipped_folders"), &s.all, theme);
    const std::string also_search_tooltip = rv_editor_text_format("pane_search.also_search",
        std::make_format_args(rv_editor_search_skipped));
    ImGui::SetItemTooltip("%s", also_search_tooltip.c_str());
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
            const std::string nothing_searched_msg = rv_editor_text_format("pane_search.nothing_searched",
                std::make_format_args(rv_editor_search_skipped));
            ImGui::TextWrapped("%s", nothing_searched_msg.c_str());
            return;
        }
        const rv_editor_search_result &r = s.result;
        // The scope stated with the result, so an empty one says where it did not look.
        const std::string project_name = app.project.root.filename().string();
        std::string scope = rv_editor_text_format("pane_search.scope_base",
            std::make_format_args(s.searched, r.files, project_name));
        if (r.skipped_dirs != 0) {
            const std::string skipped_msg = rv_editor_text_format("pane_search.scope_skipped_dirs",
                std::make_format_args(r.skipped_dirs, rv_editor_search_skipped));
            scope += skipped_msg;
        }
        if (r.skipped_files != 0) {
            const std::string files_msg = rv_editor_text_format("pane_search.scope_skipped_files",
                std::make_format_args(r.skipped_files));
            scope += files_msg;
        }
        const std::string matches_text = std::to_string(r.hits.size()) + (r.truncated ? "+" : "") + " " +
            rv_editor_text("pane_search.matches");
        const std::string count = r.hits.empty() ? rv_editor_text("pane_search.no_match") : matches_text;
        rv_editor_status(count.c_str(), r.hits.empty() ? rv_editor_status_kind::warning : rv_editor_status_kind::ok,
            theme);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", scope.c_str());
        if (r.truncated) {
            const size_t hits_count = r.hits.size();
            const std::string stopped_msg = rv_editor_text_format("pane_search.stopped_at",
                std::make_format_args(hits_count));
            ImGui::TextWrapped("%s", stopped_msg.c_str());
        }
        if (r.hits.empty()) {
            const bool show_limited = r.skipped_dirs != 0 && !s.searched_all;
            const char *msg = rv_editor_text(show_limited ? "pane_search.no_match_limited" : "pane_search.no_match_full");
            ImGui::TextWrapped("%s", msg);
            return;
        }
        rv_editor_search_list(app);
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
