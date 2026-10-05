// Status bar texts: project name, toolchain, build and runtime states.

#include "app/rv_editor_shell.hpp"

#include <string>

#include "build/rv_editor_build.hpp"
#include "session/rv_editor_session.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets.hpp"

namespace rv_editor
{

rv_editor_status_text rv_editor_status_text_make(const rv_editor_app &app)
{
    rv_editor_status_text result;

    // The project's name and the toolchain in brief; paths and versions are in
    // Window > Project Settings, the build's lines in Output.
    if (!app.project.open) {
        result.where = "No project: File > Open Project...";
    } else if (app.project.disc_title.empty()) {
        result.where = app.project.root.filename().string();
    } else {
        result.where = app.project.disc_title;
    }

    int missing = 0;
    for (const rv_editor_tool *t : { &app.tools.console, &app.tools.burner, &app.tools.baker }) {
        missing += t->problem.empty() ? 0 : 1;
    }
    result.tools = missing == 0 ? "Tools: ready" : "Tools: " + std::to_string(missing) + " missing";

    // Build and runtime are separate facts, each with the job or session it is about.
    result.build = std::string("Build: ") + rv_editor_build_state_name(app.build.state()) +
        (app.build.number() != 0 ? " #" + std::to_string(app.build.number()) : "");
    result.runtime = std::string("Runtime: ") + rv_editor_run_state_name(app.session.state()) +
        (app.session.number() != 0 ? ", session " + std::to_string(app.session.number()) : "");

    // No project: no build or session to speak of, only the way in and the tools.
    result.shown = app.project.open ? 4 : 2;
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
