// Problems: the places the latest build named plus the language servers'
// current diagnostics, each opening its file at its line in a code tile. The full
// build output stays in the Build Log.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

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

// nvim diagnostic severity shown: error and warn (info/hint are skipped).
constexpr std::string_view diag_severity_error = "error";
constexpr std::string_view diag_severity_warn = "warn";

// Problem source: "build", else the inspector or a language server (source name varies).
constexpr std::string_view problem_source_build = "build";

// Problems table columns: icon, source, file, line:col, message.
constexpr int problems_table_column_count = 5;

// The largest count and build number the summary shows in full; a count past the first reads "999+".
constexpr size_t count_cap = 999;
constexpr size_t build_number_cap = 999999;

// app.problems plus the servers' error/warning diagnostics (info/hint are skipped:
// Problems is for what blocks the build, not editor-only hints).
std::vector<rv_editor_problem> rv_editor_gather_problems(const rv_editor_app &app)
{
    std::vector<rv_editor_problem> out = app.problems;
    // The inspector reports what the build also may say of the same place: it is shown once.
    rv_editor_problems_drop_reported(out, app.inspect.problems());
    out.insert(out.end(), app.inspect.problems().begin(), app.inspect.problems().end());
    for (const auto &[file, diags] : app.nvim.diagnostics()) {
        for (const rv_editor_nvim_diagnostic &d : diags) {
            if (d.severity != diag_severity_error && d.severity != diag_severity_warn) {
                continue;
            }
            out.push_back(
                { std::filesystem::path(file), d.line, d.col, d.severity == diag_severity_error, d.message, d.source });
        }
    }
    return out;
}

std::string rv_editor_problems_count(size_t n)
{
    return n > count_cap ? std::to_string(count_cap) + "+" : std::to_string(n);
}

// The summary over the table: the build and what the build and the servers found, one bounded line.
std::string rv_editor_problems_summary(const rv_editor_build &b, size_t build_number, size_t errors, size_t warnings)
{
    const std::string e = rv_editor_problems_count(errors);
    const std::string w = rv_editor_problems_count(warnings);
    if (b.state() == rv_editor_build_state::idle) {
        return rv_editor_text_format("pane_problems.idle_status", std::make_format_args(e, w));
    }
    const char *word = b.busy() ? rv_editor_text("pane_problems.building") : rv_editor_text("pane_problems.build");
    const std::string ref = rv_editor_text_format("pane_problems.build_number", std::make_format_args(build_number));
    return rv_editor_text_format("pane_problems.build_status", std::make_format_args(word, ref, e, w));
}

// The widest row: the message box's buttons, or the longest summary.
float rv_editor_problems_content_width()
{
    float summary = 0.0f;
    for (const bool busy : { false, true }) {
        const char *word = busy ? rv_editor_text("pane_problems.building") : rv_editor_text("pane_problems.build");
        const std::string ref = rv_editor_text_format("pane_problems.build_number", std::make_format_args(build_number_cap));
        const std::string e = rv_editor_problems_count(count_cap + 1);
        const std::string text = rv_editor_text_format("pane_problems.build_status", std::make_format_args(word, ref, e, e));
        summary = std::max(summary, ImGui::CalcTextSize(text.c_str()).x);
    }
    const std::string e = rv_editor_problems_count(count_cap + 1);
    const std::string idle_text = rv_editor_text_format("pane_problems.idle_status", std::make_format_args(e, e));
    summary = std::max(summary, ImGui::CalcTextSize(idle_text.c_str()).x);
    return std::max(summary,
        rv_editor_message_box_min_width(
            { { rv_editor_text("pane_problems.alert_ok") }, { rv_editor_text("panes.open_project_box") } }));
}

// The message box: a build failure with OK, "No project is open." with its button, or nothing.
void rv_editor_problems_say(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_build_alert &alert = app.build_alert;
    const float width = rv_editor_problems_content_width();
    if (alert.kind != rv_editor_build_alert_kind::none) {
        const bool refused = alert.kind == rv_editor_build_alert_kind::not_started;
        const std::string text =
            std::string(rv_editor_text(refused ? "pane_problems.alert_not_started" : "pane_problems.alert_failed")) + "\n" +
            alert.reason;
        if (rv_editor_message_box("##alert",
                text,
                rv_editor_label_kind::error,
                { rv_editor_text("pane_problems.alert_ok") },
                theme,
                width) == 0) {
            rv_editor_app_build_alert_clear(app);
        }
    } else if (!app.project.open) {
        if (rv_editor_message_box("##alert",
                rv_editor_text("panes.no_project_open_message"),
                rv_editor_label_kind::normal,
                { rv_editor_text("panes.open_project_box") },
                theme,
                width) == 0) {
            app.open_folder_request = true;
        }
    } else {
        (void)rv_editor_message_box("##alert", "", rv_editor_label_kind::normal, {}, theme, width);
    }
}

} // namespace

rv_editor_size rv_editor_problems_minimum()
{
    const float summary = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
    return rv_editor_tiled_minimum(rv_editor_problems_content_width(), rv_editor_tiled_box_block() + summary);
}

void rv_editor_pane_problems(rv_editor_app &app, const rv_editor_theme &theme)
{
    rv_editor_problems_say(app, theme);
    const rv_editor_build &b = app.build;
    const std::vector<rv_editor_problem> problems =
        app.project.open ? rv_editor_gather_problems(app) : std::vector<rv_editor_problem>{};
    size_t errors = 0;
    for (const rv_editor_problem &p : problems) {
        errors += p.error ? 1 : 0;
    }
    // The summary's line is there with or without a project, so the table never moves.
    const auto summary_kind = errors != 0 ? rv_editor_label_kind::error :
        problems.empty()                  ? rv_editor_label_kind::normal :
                                            rv_editor_label_kind::warning;
    const std::string summary =
        app.project.open ? rv_editor_problems_summary(b, b.number(), errors, problems.size() - errors) : std::string();
    rv_editor_message_line("##summary", summary.c_str(), 1, 0.0f, theme, nullptr, summary_kind);

    (void)rv_editor_tiled_area_begin("##problems_area", rv_editor_problems_content_width(), theme);
    const rv_editor_table_column problem_columns[problems_table_column_count] = {
        { rv_editor_text("pane_problems.column_level"), rv_editor_table_width::fixed, 0.0f, 0.0f, true },
        { rv_editor_text("pane_problems.column_source") },
        { rv_editor_text("pane_problems.column_file") },
        { rv_editor_text("pane_problems.column_line") },
        { rv_editor_text("pane_problems.column_message") },
    };
    const rv_editor_table_style problem_style = { .row_bands = true,
        .header = true,
        .sizing = rv_editor_table_sizing::fixed_fit };
    if (rv_editor_table_begin("##problems", problem_columns, problems_table_column_count, problem_style)) {
        if (problems.empty()) {
            rv_editor_table_row();
            rv_editor_table_cell();
            rv_editor_content_empty_row(rv_editor_text(b.busy() ? "pane_problems.none_building" : "pane_problems.none_idle"),
                theme);
        }
        for (size_t i = 0; i < problems.size(); ++i) {
            const rv_editor_problem &p = problems[i];
            ImGui::PushID(static_cast<int>(i));
            rv_editor_table_row();
            rv_editor_table_cell();
            // A word as well as a colour.
            const bool is_error = p.error;
            const char *severity_id = is_error ? "pane_problems.severity_error" : "pane_problems.severity_warning";
            const auto kind = is_error ? rv_editor_status_kind::error : rv_editor_status_kind::warning;
            rv_editor_status(rv_editor_text(severity_id), kind, theme);
            rv_editor_table_cell();
            rv_editor_table_cell_text(p.source.c_str(), theme);
            rv_editor_table_cell();
            std::error_code ec;
            const std::filesystem::path shown = std::filesystem::relative(p.file, app.project.root, ec);
            const bool there = std::filesystem::exists(p.file, ec);
            // The whole row answers a click; double click or Enter opens the place.
            const std::string path = (ec || shown.empty() ? p.file : shown).string();
            const rv_editor_list_result row = rv_editor_list_row("##file", path.c_str(), theme, { .span_columns = true });
            if (ImGui::IsItemHovered()) {
                rv_editor_tooltip(p.file.c_str());
            }
            const bool open = row.double_clicked || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter));
            if (open && there) {
                app.open_requests.push_back({ p.file, p.line, p.column });
            }
            rv_editor_table_cell();
            const std::string line_col = std::to_string(p.line) + ":" + std::to_string(p.column);
            rv_editor_label(line_col.c_str(), rv_editor_label_kind::normal, theme, rv_editor_label_layout::line);
            rv_editor_table_cell();
            std::string message = p.message;
            const bool from_build = p.source == problem_source_build;
            if (from_build && !there) {
                message = std::string(rv_editor_text("pane_problems.prefix_file_missing")) + message;
            } else if (from_build && app.build_ended != std::filesystem::file_time_type{} &&
                std::filesystem::last_write_time(p.file, ec) > app.build_ended) {
                // The line may have moved since the build saw it.
                message = std::string(rv_editor_text("pane_problems.prefix_changed")) + message;
            }
            rv_editor_table_cell_text(message.c_str(), theme);
            ImGui::PopID();
        }
        rv_editor_table_end();
    }
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
