#include "panes/rv_editor_pane_export.hpp"

#include <cstddef>

#include "text/rv_editor_text.hpp"

namespace rv_editor
{

namespace
{

const rv_editor_export_kind_info &rv_editor_export_row(rv_editor_export_kind kind)
{
    return rv_editor_export_kinds()[static_cast<size_t>(kind)];
}

} // namespace

bool rv_editor_export_kind_change_allowed(bool busy)
{
    return !busy;
}

const char *rv_editor_export_button_why(rv_editor_export_kind kind, bool web_tools_installed, bool busy)
{
    if (busy) {
        return rv_editor_text("pane_export.busy_why");
    }
    if (rv_editor_export_row(kind).needs_web_tools && !web_tools_installed) {
        return rv_editor_text("pane_export.web_tools_why");
    }
    return nullptr;
}

rv_editor_export_result_view rv_editor_export_result_make(rv_editor_build_state state,
    const std::string &path,
    const std::string &reason,
    const rv_editor_progress &progress,
    rv_editor_export_kind kind)
{
    rv_editor_export_result_view view;
    switch (state) {
    case rv_editor_build_state::building:
    case rv_editor_build_state::cancelling:
        view.kind = rv_editor_export_result_kind::running;
        break;
    case rv_editor_build_state::succeeded:
        view.kind = rv_editor_export_result_kind::succeeded;
        view.path = path;
        view.open_browser = rv_editor_export_row(kind).open == rv_editor_export_open_action::browser;
        view.open_folder = rv_editor_export_row(kind).open == rv_editor_export_open_action::folder;
        break;
    case rv_editor_build_state::failed:
        view.kind = rv_editor_export_result_kind::failed;
        view.reason = reason;
        view.show_problems = true;
        if (!progress.runs.empty()) {
            for (const rv_editor_step &step : progress.runs.back().steps) {
                if (step.state == rv_editor_step_state::failed || step.state == rv_editor_step_state::interrupted) {
                    view.step = step.label;
                    break;
                }
            }
        }
        break;
    default:
        break;
    }
    return view;
}

void rv_editor_export_show_problems(rv_editor_app &app)
{
    app.show_request = rv_editor_pane_kind::inspector_report;
}

} // namespace rv_editor
