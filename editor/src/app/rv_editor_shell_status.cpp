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

// What the bar calls the open project: its disc title, else its folder.
std::string rv_editor_status_where(const rv_editor_app &app)
{
    if (!app.project.open) {
        return rv_editor_text("shell_status.no_project");
    }
    if (app.project.disc_title.empty()) {
        return app.project.root.filename().string();
    }
    return app.project.disc_title;
}

} // namespace

rv_editor_status_text rv_editor_status_text_make(const rv_editor_app &app)
{
    // The project's name and the toolchain in brief; paths and versions are in
    // Window > Project Settings, the build's lines in Output.
    const std::string where = rv_editor_status_where(app);

    int missing = 0;
    for (const rv_editor_tool *t : { &app.tools.console, &app.tools.burner, &app.tools.baker }) {
        missing += t->problem.empty() ? 0 : 1;
    }
    const std::string tools = (missing == 0) ?
        std::string(rv_editor_text("shell_status.tools_ready")) :
        rv_editor_text_format("shell_status.tools_missing", std::make_format_args(missing));

    // Build and runtime are separate facts, each with the job or session it is about.
    const char *const build_state = rv_editor_build_state_name(app.build.state());
    const auto build_number = app.build.number();
    const std::string build = (build_number != 0) ?
        rv_editor_text_format("shell_status.build_with_number", std::make_format_args(build_state, build_number)) :
        rv_editor_text_format("shell_status.build_no_number", std::make_format_args(build_state));

    const char *const runtime_state = rv_editor_run_state_name(app.session.state());
    const auto session_number = app.session.number();
    const std::string runtime = (session_number != 0) ?
        rv_editor_text_format("shell_status.runtime_with_number", std::make_format_args(runtime_state, session_number)) :
        rv_editor_text_format("shell_status.runtime_no_number", std::make_format_args(runtime_state));

    // No project: no build or session to speak of, only the way in and the tools.
    return { where, tools, build, runtime, app.project.open ? status_fields_project : status_fields_no_project };
}

void rv_editor_shell_status(const rv_editor_shell &shell, const rv_editor_theme &theme)
{
    const rv_editor_status_text text = rv_editor_status_text_make(shell.app);
    const char *const fields[] = { text.where.c_str(), text.tools.c_str(), text.build.c_str(), text.runtime.c_str() };
    rv_editor_status_bar(fields, text.shown, theme);
}

} // namespace rv_editor
