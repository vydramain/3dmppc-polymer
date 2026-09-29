// Problems: the places the latest build named (BLD-06) plus the language servers'
// current diagnostics, each opening its file at its line in a code tile. The full
// build output stays in the Build Log.

#include "panes/rv_editor_panes.hpp"

#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// app.problems plus the servers' error/warning diagnostics (info/hint are skipped:
// Problems is for what blocks the build, not editor-only hints).
std::vector<rv_editor_problem> rv_editor_gather_problems(const rv_editor_app &app)
{
    std::vector<rv_editor_problem> out = app.problems;
    for (const auto &[file, diags] : app.nvim.diagnostics()) {
        for (const rv_editor_nvim_diagnostic &d : diags) {
            if (d.severity != "error" && d.severity != "warn") {
                continue;
            }
            out.push_back({ std::filesystem::path(file), d.line, d.col, d.severity == "error", d.message,
                d.source });
        }
    }
    return out;
}

} // namespace

void rv_editor_pane_problems(rv_editor_app &app, const rv_editor_theme &theme)
{
    if (!app.project.open) {
        rv_editor_open_project_row(app, theme);
        return;
    }
    const rv_editor_build &b = app.build;
    if (b.state() == rv_editor_build_state::idle) {
        ImGui::TextWrapped("No build in this window yet: Build lists here what the compiler finds.");
        return;
    }
    const std::vector<rv_editor_problem> problems = rv_editor_gather_problems(app);
    size_t build_errors = 0;
    size_t build_total = 0;
    size_t lsp_errors = 0;
    size_t lsp_total = 0;
    for (const rv_editor_problem &p : problems) {
        if (p.source == "build") {
            build_errors += p.error ? 1 : 0;
            ++build_total;
        } else {
            lsp_errors += p.error ? 1 : 0;
            ++lsp_total;
        }
    }
    std::string what = std::string(b.busy() ? "Building" : "Build") +
        (b.image().empty() ? " #" + std::to_string(b.number()) : " of " + b.image().filename().string()) + ": " +
        std::to_string(build_errors) + " errors, " + std::to_string(build_total - build_errors) + " warnings";
    if (lsp_total != 0) {
        what += "; language servers: " + std::to_string(lsp_errors) + " errors, " +
            std::to_string(lsp_total - lsp_errors) + " warnings";
    }
    const size_t errors = build_errors + lsp_errors;
    rv_editor_status(what.c_str(), errors != 0 ? rv_editor_status_kind::error
            : problems.empty()               ? rv_editor_status_kind::ok
                                             : rv_editor_status_kind::warning,
        theme);
    if (problems.empty()) {
        ImGui::TextWrapped(b.busy() ? "None so far." : "None with a place. Anything else is in the Build Log.");
        return;
    }
    if (!ImGui::BeginTable("##problems", 5,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        return;
    }
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Message");
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < problems.size(); ++i) {
        const rv_editor_problem &p = problems[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        // A word as well as a colour (UX-05).
        rv_editor_status(p.error ? "error" : "warning", p.error ? rv_editor_status_kind::error : rv_editor_status_kind::warning,
            theme);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(p.source.c_str());
        ImGui::TableNextColumn();
        std::error_code ec;
        const std::filesystem::path shown = std::filesystem::relative(p.file, app.project.root, ec);
        const bool there = std::filesystem::exists(p.file, ec);
        // The whole row answers a click; double click or Enter opens the place.
        const bool pressed = ImGui::Selectable((ec || shown.empty() ? p.file : shown).c_str(), false,
            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", p.file.c_str());
        }
        const bool open = (pressed && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ||
            (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter));
        if (open && there) {
            app.open_requests.push_back({ p.file, p.line, p.column });
        }
        ImGui::TableNextColumn();
        ImGui::Text("%d:%d", p.line, p.column);
        ImGui::TableNextColumn();
        std::string message = p.message;
        const bool from_build = p.source == "build";
        if (from_build && !there) {
            message = "(file missing) " + message;
        } else if (from_build && app.build_ended != std::filesystem::file_time_type{} &&
            std::filesystem::last_write_time(p.file, ec) > app.build_ended) {
            // The line may have moved since the build saw it.
            message = "(changed since this build) " + message;
        }
        ImGui::TextUnformatted(message.c_str());
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace rv_editor
