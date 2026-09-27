// Search Results: the query, where it looked, and each file:line holding it, opening
// in a code tile (TXT-06). Run on Enter or Find, never while typing.

#include "panes/rv_editor_panes.hpp"

#include <string>
#include <system_error>

#include "imgui.h"

#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

void rv_editor_search_list(rv_editor_app &app)
{
    const rv_editor_search_result &r = app.project_search.result;
    if (!ImGui::BeginTable("##hits", 3,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        return;
    }
    ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Text");
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
    rv_editor_flow(ImGui::GetFontSize() * 14.0f);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    if (s.focus) {
        ImGui::SetKeyboardFocusHere();
        s.focus = false;
    }
    rv_editor_text_field("##query", s.query, sizeof(s.query), theme);
    ImGui::SetItemTooltip("Text to find in the project's files (Ctrl+Shift+F); Enter searches");
    // A single-line field lets go of the keyboard on Enter.
    bool run = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    rv_editor_flow(rv_editor_button_width("Find"));
    const char *why_not = s.query[0] == '\0' ? "Type the text to find first" : nullptr;
    run |= rv_editor_button("Find", theme, { rv_editor_look::live, why_not });
    rv_editor_flow(rv_editor_checkbox_width("Match Case"));
    rv_editor_checkbox("Match Case", &s.match_case, theme);
    rv_editor_flow(rv_editor_checkbox_width("Skipped Folders Too"));
    rv_editor_checkbox("Skipped Folders Too", &s.all, theme);
    ImGui::SetItemTooltip("Also search %s", rv_editor_search_skipped);
    if (run && s.query[0] != '\0') {
        // ponytail: runs on the UI thread; a worker thread when projects grow past a blink.
        s.result = rv_editor_search_run(app.project.root, s.query, s.match_case, s.all);
        s.searched = s.query;
        s.searched_all = s.all;
    }
    if (s.searched.empty()) {
        ImGui::TextWrapped("Nothing searched yet. Folders left out unless ticked: %s.", rv_editor_search_skipped);
        return;
    }
    const rv_editor_search_result &r = s.result;
    // The scope stated with the result, so an empty one says where it did not look.
    std::string scope = "\"" + s.searched + "\" in " + std::to_string(r.files) + " files of " +
        app.project.root.filename().string();
    if (r.skipped_dirs != 0) {
        scope += "; " + std::to_string(r.skipped_dirs) + " folders left out (" + rv_editor_search_skipped + ")";
    }
    if (r.skipped_files != 0) {
        scope += "; " + std::to_string(r.skipped_files) + " binary or large files left out";
    }
    const std::string count = r.hits.empty() ? "No match" : std::to_string(r.hits.size()) + (r.truncated ? "+" : "") +
        " matches";
    rv_editor_status(count.c_str(), r.hits.empty() ? rv_editor_status_kind::warning : rv_editor_status_kind::ok, theme);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", scope.c_str());
    if (r.truncated) {
        ImGui::TextWrapped("Stopped at %zu matches: a longer query narrows it.", r.hits.size());
    }
    if (r.hits.empty()) {
        ImGui::TextWrapped(r.skipped_dirs != 0 && !s.searched_all
                ? "No match where it looked. Tick Skipped Folders Too to look in the folders left out."
                : "No match anywhere in the project.");
        return;
    }
    rv_editor_search_list(app);
}

} // namespace rv_editor
