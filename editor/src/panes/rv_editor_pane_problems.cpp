// Problems: the places the latest build named plus the language servers'
// current diagnostics, each opening its file at its line in a code tile. The full
// build output stays in the Build Log.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "panes/rv_editor_pane_tiled.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_message.hpp"
#include "ui/rv_editor_widgets_status.hpp"

namespace rv_editor
{

namespace
{

// nvim diagnostic severity shown: error and warn (info/hint are skipped).
constexpr std::string_view diag_severity_error = "error";
constexpr std::string_view diag_severity_warn = "warn";

// Problem source: "build", else the inspector or a language server (source name varies).
constexpr std::string_view problem_source_build = "build";

// Problems table columns: level icon, File:line, message.
constexpr int problems_table_column_count = 3;

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

// "path:line", the path relative to the project when it is under it.
std::string rv_editor_problem_place(const rv_editor_app &app, const rv_editor_problem &p)
{
    std::error_code ec;
    const std::filesystem::path shown = std::filesystem::relative(p.file, app.project.root, ec);
    return (ec || shown.empty() ? p.file : shown).string() + ":" + std::to_string(p.line);
}

// The strip under the table: the build's failure or refusal with OK, the closed project's question with its button.
// Empty text: no strip.
rv_editor_message_say rv_editor_problems_strip_of(const rv_editor_app &app)
{
    if (app.build_alert.kind != rv_editor_build_alert_kind::none) {
        return { rv_editor_problems_alert_text(app),
            rv_editor_label_kind::error,
            { rv_editor_text("pane_problems.alert_ok") } };
    }
    if (!app.project.open) {
        return { rv_editor_text("panes.no_project_open_message"),
            rv_editor_label_kind::normal,
            { rv_editor_text("panes.open_project_box") } };
    }
    return {};
}

// The narrowest pane: the widest row of the strip's buttons.
float rv_editor_problems_content_width()
{
    return rv_editor_message_strip_min_width(
        { { rv_editor_text("pane_problems.alert_ok") }, { rv_editor_text("panes.open_project_box") } });
}

} // namespace

std::string rv_editor_problems_alert_text(const rv_editor_app &app)
{
    const rv_editor_build_alert &alert = app.build_alert;
    switch (alert.kind) {
    case rv_editor_build_alert_kind::none:
        return {};
    case rv_editor_build_alert_kind::not_started:
        return rv_editor_text_format("pane_problems.alert_not_started", std::make_format_args(alert.reason));
    case rv_editor_build_alert_kind::failed:
        break;
    }
    const size_t number = app.build.number();
    return rv_editor_text_format("pane_problems.alert_failed", std::make_format_args(number, alert.reason));
}

std::string rv_editor_problems_level_tip(const rv_editor_problem &problem)
{
    const std::string level = rv_editor_text(problem.error ? "pane_problems.severity_error" : "pane_problems.severity_warning");
    return problem.source.empty() ?
        level :
        rv_editor_text_format("pane_problems.level_tip", std::make_format_args(level, problem.source));
}

rv_editor_size rv_editor_problems_minimum()
{
    // The strip at its tallest: three lines of text, a longer one scrolls inside it.
    return rv_editor_tiled_minimum(rv_editor_problems_content_width(), rv_editor_tiled_strip_block(false));
}

void rv_editor_pane_problems(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_build &b = app.build;
    const std::vector<rv_editor_problem> problems =
        app.project.open ? rv_editor_gather_problems(app) : std::vector<rv_editor_problem>{};
    const rv_editor_message_say strip = rv_editor_problems_strip_of(app);
    const float strip_h = rv_editor_message_strip_height(strip.text, strip.buttons, ImGui::GetContentRegionAvail().x);

    (void)rv_editor_tiled_area_begin("##problems_area", rv_editor_problems_content_width(), theme, {}, false, 0.0f, strip_h);
    // The icon, the place and the message, which wraps and takes what the first two leave.
    // The place column is as wide as its widest text, so the message column's share is what is really left.
    float place_w = ImGui::CalcTextSize(rv_editor_text("pane_problems.column_file")).x;
    for (const rv_editor_problem &p : problems) {
        place_w = std::max(place_w, ImGui::CalcTextSize(rv_editor_problem_place(app, p).c_str()).x);
    }
    place_w += 2.0f * (ImGui::GetStyle().CellPadding.x + ImGui::GetStyle().FramePadding.x);
    const rv_editor_table_column problem_columns[problems_table_column_count] = {
        { "", rv_editor_table_width::fixed },
        { rv_editor_text("pane_problems.column_file"), rv_editor_table_width::fixed, place_w },
        { rv_editor_text("pane_problems.column_message"), rv_editor_table_width::stretch, 0.0f, 1.0f },
    };
    const rv_editor_table_style problem_style = { .row_bands = true, .header = true };
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
            const auto kind = p.error ? rv_editor_status_kind::error : rv_editor_status_kind::warning;
            rv_editor_status("", kind, theme);
            const ImRect cell = ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), 0);
            if (ImGui::IsMouseHoveringRect(cell.Min, cell.Max)) {
                rv_editor_tooltip(rv_editor_problems_level_tip(p).c_str());
            }
            rv_editor_table_cell();
            std::error_code ec;
            const bool there = std::filesystem::exists(p.file, ec);
            // The whole row answers a click; double click or Enter opens the place.
            const std::string path = rv_editor_problem_place(app, p);
            const rv_editor_list_result row = rv_editor_list_row("##file", path.c_str(), theme, { .span_columns = true });
            if (ImGui::IsItemHovered()) {
                rv_editor_tooltip(p.file.c_str());
            }
            const bool open = row.double_clicked || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter));
            if (open && there) {
                app.open_requests.push_back({ p.file, p.line, p.column });
            }
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
            rv_editor_label(message.c_str(), rv_editor_label_kind::normal, theme);
            ImGui::PopID();
        }
        rv_editor_table_end();
    }
    rv_editor_tiled_area_end(theme);
    const int pressed = rv_editor_message_strip("##strip", strip.text, strip.kind, strip.buttons, theme);
    if (pressed == 0 && app.build_alert.kind != rv_editor_build_alert_kind::none) {
        rv_editor_app_build_alert_clear(app);
    } else if (pressed == 0) {
        app.open_folder_request = true;
    }
}

} // namespace rv_editor
