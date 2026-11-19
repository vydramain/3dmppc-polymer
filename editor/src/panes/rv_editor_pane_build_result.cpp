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
#include "ui/rv_editor_widgets_message.hpp"

namespace rv_editor
{

namespace
{

// Separator between state label and status text in build status line
constexpr const char *state_separator = ": ";

// Log source name for build and candidate build diagnostics
constexpr const char *log_source_build = "build";

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

// The shelf's three buttons, the widest row of the pane with the box's.
float rv_editor_build_result_content_width()
{
    return std::max(rv_editor_message_box_min_width({ { rv_editor_text("panes.open_project_box") },
                        { rv_editor_text("pane_build_result.open_log_button") } }),
        rv_editor_say_shelf_width({ rv_editor_text("pane_build_result.build_button"),
            rv_editor_text("pane_build_result.build_candidate_button"),
            rv_editor_text("pane_build_result.cancel_button") }));
}

// Build, Build Candidate and Cancel Build in one row, each in its place whatever the job does.
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

// What the box says of the job: nothing built yet, or what it is and how it ended, with Open Build Log when it kept one.
void rv_editor_build_result_say(rv_editor_app &app, size_t errors, size_t warnings, const rv_editor_theme &theme, float width)
{
    const rv_editor_build &b = app.build;
    if (b.state() == rv_editor_build_state::idle) {
        (void)rv_editor_message_box("##say",
            rv_editor_text("pane_build_result.nothing_built"),
            rv_editor_label_kind::normal,
            {},
            theme,
            width);
        return;
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
    std::string file;
    if (done) {
        std::string exit_line = rv_editor_exit_text(b.exit_status());
        exit_line += b.output_cut() ? rv_editor_text("pane_build_result.output_cut") : "";
        second = exit_line + "; " + second;
        file = app.log_files.path_for(b.pid());
    }
    const std::vector<std::string> buttons = file.empty() ?
        std::vector<std::string>{} :
        std::vector<std::string>{ rv_editor_text("pane_build_result.open_log_button") };
    const std::string text = what + state_separator + rv_editor_build_state_name(b.state()) + "\n" + second;
    if (rv_editor_message_box("##say", text, rv_editor_build_outcome_kind(b.state()), buttons, theme, width) == 0) {
        app.open_requests.push_back({ file, 0 });
    }
}

} // namespace

rv_editor_size rv_editor_build_result_minimum()
{
    return rv_editor_tiled_minimum(rv_editor_build_result_content_width(),
        rv_editor_tiled_shelf_block() + rv_editor_tiled_box_block());
}

void rv_editor_pane_build_result(rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_build &b = app.build;
    const float width = rv_editor_build_result_content_width();
    rv_editor_build_result_shelf(app, theme);
    size_t errors = 0;
    size_t warnings = 0;
    const std::vector<const rv_editor_log_line *> found = rv_editor_build_result_found(app, errors, warnings);
    if (!rv_editor_say_no_project(app, theme, width)) {
        rv_editor_build_result_say(app, errors, warnings, theme, width);
    }
    (void)rv_editor_tiled_area_begin("##log_area", width, theme);
    if (app.project.open && b.state() != rv_editor_build_state::idle) {
        for (const rv_editor_log_line *l : found) {
            rv_editor_log_row("",
                log_source_build,
                l->level == rv_editor_log_level::error ? rv_editor_severity::error : rv_editor_severity::warning,
                l->text.c_str(),
                theme);
        }
    }
    rv_editor_tiled_area_end(theme);
}

} // namespace rv_editor
