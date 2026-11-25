// Build Result: the centre of Burn's Build & Diagnose - what the last build job
// did, its errors and warnings, and what to do next; never an empty Untitled.

#include "panes/rv_editor_panes.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "panes/rv_editor_pane_say.hpp"
#include "panes/rv_editor_pane_tiled.hpp"
#include "platform/rv_editor_process.hpp"
#include "text/rv_editor_text.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_content.hpp"
#include "ui/rv_editor_widgets_message.hpp"
#include "ui/rv_editor_widgets_steps.hpp"

namespace rv_editor
{

namespace
{

// Separator between state label and status text in build status line
constexpr const char *state_separator = ": ";

// Log source name for build and candidate build diagnostics
constexpr const char *log_source_build = "build";

// Progress for a running job, and its state once it starts cancelling or ends.
std::string rv_editor_build_detail(const rv_editor_build &build)
{
    const std::string step =
        build.state() == rv_editor_build_state::building ? rv_editor_steps_compact(build.progress()) : std::string();
    return step.empty() ? rv_editor_build_state_name(build.state()) : step;
}

// The label kind of a build's outcome.
rv_editor_label_kind rv_editor_build_outcome_kind(rv_editor_build_state state)
{
    switch (state) {
    case rv_editor_build_state::succeeded:
        return rv_editor_label_kind::info;
    case rv_editor_build_state::failed:
        return rv_editor_label_kind::error;
    case rv_editor_build_state::cancelled:
        return rv_editor_label_kind::warning;
    default:
        return rv_editor_label_kind::normal;
    }
}

// Measure the shelf buttons and the result strip.
float rv_editor_build_result_content_width()
{
    return std::max(rv_editor_message_strip_min_width({ { rv_editor_text("panes.open_project_box") },
                        { rv_editor_text("pane_build_result.open_log_button") } }),
        rv_editor_say_shelf_width({ rv_editor_text("pane_build_result.build_button"),
            rv_editor_text("pane_build_result.build_candidate_button"),
            rv_editor_text("pane_build_result.cancel_button"),
            rv_editor_text("pane_build_result.run_last_button") }));
}

// The build actions stay in place whatever the job does.
void rv_editor_build_result_shelf(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_build &b = app.build;
    rv_editor_shelf_begin("##shelf", theme);
    const char *why_not = rv_editor_app_why_not_build(app);
    if (rv_editor_button(rv_editor_text("pane_build_result.build_button"), theme, { rv_editor_look::live, why_not })) {
        rv_editor_app_build(app);
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_build_result.build_candidate_button"),
            theme,
            { rv_editor_look::live, why_not })) {
        rv_editor_app_build_candidate(app);
    }
    ImGui::SameLine();
    const bool cancel_pending = rv_editor_app_request_pending(app, rv_editor_pane_request_kind::cancel_build);
    const bool cancelling = b.state() == rv_editor_build_state::cancelling;
    const char *no_build_why =
        b.state() == rv_editor_build_state::building ? nullptr : rv_editor_text("pane_build_result.no_build_running");
    const char *cancelling_why = cancelling ? rv_editor_text("panes.already_cancelling") : no_build_why;
    const char *cancel_why = cancel_pending ? rv_editor_text("app_why_not.cancel_pending") : cancelling_why;
    if (rv_editor_button(rv_editor_text("pane_build_result.cancel_button"), theme, { rv_editor_look::live, cancel_why })) {
        (void)rv_editor_app_request(app, { rv_editor_pane_request_kind::cancel_build });
    }
    ImGui::SameLine();
    if (rv_editor_button(rv_editor_text("pane_build_result.run_last_button"),
            theme,
            { rv_editor_look::live, rv_editor_app_why_not_run_last(app) })) {
        rv_editor_app_run_last(app);
    }
    rv_editor_shelf_end();
}

// The job's own error and warning lines, newest job only; all of them stay in the Build Log.
std::vector<const rv_editor_log_line *> rv_editor_build_result_found(const rv_editor_app &app, size_t &errors, size_t &warnings)
{
    std::vector<const rv_editor_log_line *> found;
    for (const rv_editor_log_line &l : app.log.lines()) {
        if (l.seq < app.build_first_seq ||
            (l.source != rv_editor_log_source::build && l.source != rv_editor_log_source::candidate) ||
            l.level == rv_editor_log_level::info) {
            continue;
        }
        (l.level == rv_editor_log_level::error ? errors : warnings) += 1;
        found.push_back(&l);
    }
    return found;
}

// What the strip says of the job: what it is and how it ended, with Open Build Log when it kept one. Nothing built yet is
// no message: the area says it. `file` is the log's file, empty when there is none.
rv_editor_message_say rv_editor_build_result_say(const rv_editor_app &app, size_t errors, size_t warnings, std::string &file)
{
    const rv_editor_build &b = app.build;
    if (b.state() == rv_editor_build_state::idle) {
        return {};
    }
    std::string what;
    if (b.image().empty()) {
        const auto build_num = b.number();
        what = rv_editor_text_format("pane_build_result.dev_build", std::make_format_args(build_num));
    } else {
        const auto img_name = b.image().filename().string();
        what = rv_editor_text_format("pane_build_result.candidate_image", std::make_format_args(img_name));
    }
    const bool done = !b.busy();
    std::string second = rv_editor_text_format("pane_build_result.errors_warnings", std::make_format_args(errors, warnings));
    if (done) {
        std::string exit_line = rv_editor_exit_text(b.exit_status());
        exit_line += b.output_cut() ? rv_editor_text("pane_build_result.output_cut") : "";
        second = exit_line + "; " + second;
        file = app.log_files.path_for(b.pid());
    }
    return { what + state_separator + rv_editor_build_detail(b) + "\n" + second,
        rv_editor_build_outcome_kind(b.state()),
        file.empty() ? std::vector<std::string>{} :
                       std::vector<std::string>{ rv_editor_text("pane_build_result.open_log_button") } };
}

} // namespace

std::string rv_editor_build_status(const rv_editor_build &build)
{
    if (build.state() == rv_editor_build_state::idle) {
        return {};
    }
    const auto number = build.target_number();
    const std::string detail = rv_editor_build_detail(build);
    if (build.image().empty()) {
        return rv_editor_text_format("panes.status_build", std::make_format_args(number, detail));
    }
    const auto filename = build.image().filename().string();
    const std::string candidate = number != 0 ?
        rv_editor_text_format("pane_candidate.facts_title", std::make_format_args(number)) :
        rv_editor_text_format("pane_build_result.candidate_image", std::make_format_args(filename));
    return candidate + " " + detail;
}

rv_editor_size rv_editor_build_result_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_build_result_content_width(),
        rv_editor_tiled_shelf_block() + rv_editor_tiled_strip_block());
}

void rv_editor_pane_build_result(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_build &b = app.build;
    const float width = rv_editor_build_result_content_width();
    rv_editor_build_result_shelf(app, theme);
    size_t errors = 0;
    size_t warnings = 0;
    const std::vector<const rv_editor_log_line *> found = rv_editor_build_result_found(app, errors, warnings);
    std::string file;
    const rv_editor_message_say say =
        app.project.open ? rv_editor_build_result_say(app, errors, warnings, file) : rv_editor_say_no_project();
    const float strip_w = std::max(ImGui::GetContentRegionAvail().x, width);
    (void)rv_editor_tiled_area_begin("##log_area",
        width,
        theme,
        {},
        false,
        0.0f,
        rv_editor_message_strip_height(say.text, say.buttons, strip_w));
    if (app.project.open && b.state() == rv_editor_build_state::idle) {
        rv_editor_content_empty_row(rv_editor_text("pane_build_result.nothing_built"), theme);
    } else if (app.project.open) {
        for (const rv_editor_log_line *l : found) {
            rv_editor_log_row("",
                log_source_build,
                l->level == rv_editor_log_level::error ? rv_editor_severity::error : rv_editor_severity::warning,
                l->text.c_str(),
                theme);
        }
    }
    rv_editor_tiled_area_end(theme);
    if (rv_editor_message_strip("##strip", say.text, say.kind, say.buttons, theme, strip_w) == 0) {
        if (!app.project.open) {
            app.open_folder_request = true;
        } else {
            app.open_requests.push_back({ file, 0 });
        }
    }
}

} // namespace rv_editor
