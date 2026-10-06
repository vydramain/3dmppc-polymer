// Problems: the places the latest build named plus the language servers'
// current diagnostics, each opening its file at its line in a code tile. The full
// build output stays in the Build Log.

#include "panes/rv_editor_panes.hpp"

#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// nvim diagnostic severity shown: error and warn (info/hint are skipped).
constexpr std::string_view diag_severity_error = "error";
constexpr std::string_view diag_severity_warn = "warn";

// Problem source: "build" vs language server (source name varies).
constexpr std::string_view problem_source_build = "build";

// Problems table columns: icon, source, file, line:col, message.
constexpr int problems_table_column_count = 5;

// app.problems plus the servers' error/warning diagnostics (info/hint are skipped:
// Problems is for what blocks the build, not editor-only hints).
std::vector<rv_editor_problem> rv_editor_gather_problems(const rv_editor_app &app)
{
    std::vector<rv_editor_problem> out = app.problems;
    for (const auto &[file, diags] : app.nvim.diagnostics()) {
        for (const rv_editor_nvim_diagnostic &d : diags) {
            if (d.severity != diag_severity_error && d.severity != diag_severity_warn) {
                continue;
            }
            out.push_back({ std::filesystem::path(file), d.line, d.col, d.severity == diag_severity_error,
                d.message, d.source });
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
    const std::vector<rv_editor_problem> problems = rv_editor_gather_problems(app);
    if (b.state() == rv_editor_build_state::idle && problems.empty()) {
        ImGui::TextWrapped("%s", rv_editor_text("pane_problems.heading_empty"));
        return;
    }
    size_t build_errors = 0;
    size_t build_total = 0;
    size_t lsp_errors = 0;
    size_t lsp_total = 0;
    for (const rv_editor_problem &p : problems) {
        if (p.source == problem_source_build) {
            build_errors += p.error ? 1 : 0;
            ++build_total;
        } else {
            lsp_errors += p.error ? 1 : 0;
            ++lsp_total;
        }
    }
    std::string what;
    if (b.state() == rv_editor_build_state::idle) {
        // Idle with diagnostics: show only language server info without pretending it's a build result.
        const size_t lsp_warnings = lsp_total - lsp_errors;
        what = rv_editor_text_format("pane_problems.idle_servers",
            std::make_format_args(lsp_errors, lsp_warnings));
    } else {
        const char *build_word = b.busy() ? rv_editor_text("pane_problems.building") : rv_editor_text("pane_problems.build");
        std::string build_ref;
        const size_t build_number = b.number();
        if (b.image().empty()) {
            build_ref = rv_editor_text_format("pane_problems.build_number", std::make_format_args(build_number));
        } else {
            const std::string image_name = b.image().filename().string();
            build_ref = rv_editor_text_format("pane_problems.build_image", std::make_format_args(image_name));
        }
        const size_t build_warnings = build_total - build_errors;
        what = rv_editor_text_format("pane_problems.build_status",
            std::make_format_args(build_word, build_ref, build_errors, build_warnings));
        if (lsp_total != 0) {
            const size_t lsp_warnings = lsp_total - lsp_errors;
            what += rv_editor_text_format("pane_problems.servers_suffix",
                std::make_format_args(lsp_errors, lsp_warnings));
        }
    }
    const size_t errors = build_errors + lsp_errors;

    rv_editor_well_begin("##well", ImVec2(0, 0), theme);
    const auto body = [&]() {
        rv_editor_status(what.c_str(), errors != 0 ? rv_editor_status_kind::error
                : problems.empty()               ? rv_editor_status_kind::ok
                                                 : rv_editor_status_kind::warning,
            theme);
        if (problems.empty()) {
            const char *id = b.busy() ? "pane_problems.none_building" : "pane_problems.none_idle";
            ImGui::TextWrapped("%s", rv_editor_text(id));
            return;
        }
        if (!ImGui::BeginTable("##problems", problems_table_column_count,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            return;
        }
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(rv_editor_text("pane_problems.column_source"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(rv_editor_text("pane_problems.column_file"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(rv_editor_text("pane_problems.column_line"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(rv_editor_text("pane_problems.column_message"));
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < problems.size(); ++i) {
            const rv_editor_problem &p = problems[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // A word as well as a colour.
            const bool is_error = p.error;
            const char *severity_id = is_error ? "pane_problems.severity_error" : "pane_problems.severity_warning";
            const auto kind = is_error ? rv_editor_status_kind::error : rv_editor_status_kind::warning;
            rv_editor_status(rv_editor_text(severity_id), kind, theme);
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
            const bool from_build = p.source == problem_source_build;
            if (from_build && !there) {
                message = std::string(rv_editor_text("pane_problems.prefix_file_missing")) + message;
            } else if (from_build && app.build_ended != std::filesystem::file_time_type{} &&
                std::filesystem::last_write_time(p.file, ec) > app.build_ended) {
                // The line may have moved since the build saw it.
                message = std::string(rv_editor_text("pane_problems.prefix_changed")) + message;
            }
            ImGui::TextUnformatted(message.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    };
    body();
    rv_editor_well_end();
}

} // namespace rv_editor
