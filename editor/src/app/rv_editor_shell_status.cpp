// Status bar texts: project name, toolchain, build and runtime states.

#include "app/rv_editor_shell.hpp"

#include <string>

#include "build/rv_editor_build.hpp"
#include "session/rv_editor_session.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

namespace
{

// Status bar fields shown with a project open.
constexpr int status_fields_project = 4;

// Status bar fields shown without a project.
constexpr int status_fields_no_project = 2;

} // namespace

rv_editor_status_text rv_editor_status_text_make(const rv_editor_app &app)
{
    rv_editor_status_text result;

    // The project's name and the toolchain in brief; paths and versions are in
    // Window > Project Settings, the build's lines in Output.
    if (!app.project.open) {
        result.where = rv_editor_text("shell_status.no_project");
    } else if (app.project.disc_title.empty()) {
        result.where = app.project.root.filename().string();
    } else {
        result.where = app.project.disc_title;
    }

    int missing = 0;
    for (const rv_editor_tool *t : { &app.tools.console, &app.tools.burner, &app.tools.baker }) {
        missing += t->problem.empty() ? 0 : 1;
    }
    if (missing == 0) {
        result.tools = rv_editor_text("shell_status.tools_ready");
    } else {
        const auto m = missing;
        result.tools = rv_editor_text_format("shell_status.tools_missing", std::make_format_args(m));
    }

    // Build and runtime are separate facts, each with the job or session it is about.
    const char *const build_state = rv_editor_build_state_name(app.build.state());
    if (app.build.number() != 0) {
        const auto b = app.build.number();
        result.build = rv_editor_text_format("shell_status.build_with_number",
            std::make_format_args(build_state, b));
    } else {
        result.build = rv_editor_text_format("shell_status.build_no_number",
            std::make_format_args(build_state));
    }

    const char *const runtime_state = rv_editor_run_state_name(app.session.state());
    if (app.session.number() != 0) {
        const auto s = app.session.number();
        result.runtime = rv_editor_text_format("shell_status.runtime_with_number",
            std::make_format_args(runtime_state, s));
    } else {
        result.runtime = rv_editor_text_format("shell_status.runtime_no_number",
            std::make_format_args(runtime_state));
    }

    // No project: no build or session to speak of, only the way in and the tools.
    result.shown = app.project.open ? status_fields_project : status_fields_no_project;
    return result;
}

void rv_editor_shell_status(const rv_editor_shell &shell, const rv_editor_theme &theme)
{
    const rv_editor_status_text text = rv_editor_status_text_make(shell.app);
    const char *const fields[] = { text.where.c_str(), text.tools.c_str(), text.build.c_str(),
        text.runtime.c_str() };
    rv_editor_status_bar(fields, text.shown, theme);
}

} // namespace rv_editor
