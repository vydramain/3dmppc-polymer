// Status bar texts: project name, toolchain, build and runtime states.

#include "app/rv_editor_shell.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <utility>

#include "imgui.h"
#include "build/rv_editor_build.hpp"
#include "pdklib/rv_progress/rv_progress.hpp"
#include "session/rv_editor_session.hpp"
#include "text/rv_editor_text.hpp"
#include "theme/rv_editor_theme.hpp"
#include "ui/rv_editor_widgets.hpp"
#include "ui/rv_editor_widgets_steps.hpp"

namespace rv_editor
{

namespace
{

// Status bar fields shown with a project open.
constexpr int status_fields_project = 4;

// Status bar fields shown without a project.
constexpr int status_fields_no_project = 2;

// A count the bar shows as itself up to this and as "999+" above it.
constexpr int status_count_cap = 999;

// The most digits a build or session number takes: six.
constexpr int status_number_limit = 999999;

// The Tools segment's place in the bar.
constexpr int status_tools_index = 1;

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
    // Project > Project Settings, the build's lines in Output.
    int missing = 0;
    for (rv_editor_tool rv_editor_toolchain::*const member : rv_editor_toolchain_tools) {
        missing += (app.tools.*member).problem.empty() ? 0 : 1;
    }

    // Build and runtime are separate facts, each with the job or session it is about.
    const char *const build_state = rv_editor_build_state_name(app.build.state());
    const auto build_number = app.build.number();
    std::string build = rv_editor_text_format("shell_status.build_no_number", std::make_format_args(build_state));
    if (build_number != 0) {
        // The step line while it is shown; the state's name when there is none (done, failed before a step, cancelled).
        std::string detail = build_state;
        if (app.build.busy() || app.build.state() == rv_editor_build_state::failed) {
            const std::string steps = rv_editor_steps_compact(app.build.progress());
            detail = steps.empty() ? detail : steps;
        }
        build = rv_editor_text_format("shell_status.build_with_number", std::make_format_args(build_number, detail));
    }

    const char *const runtime_state = rv_editor_run_state_name(app.session.state());
    const auto session_number = app.session.number();
    const std::string runtime = (session_number != 0) ?
        rv_editor_text_format("shell_status.runtime_with_number", std::make_format_args(runtime_state, session_number)) :
        rv_editor_text_format("shell_status.runtime_no_number", std::make_format_args(runtime_state));

    // No project: no build or session to speak of, only the way in and the tools.
    return { rv_editor_status_where(app),
        rv_editor_status_tools_text(missing),
        build,
        runtime,
        app.project.open ? status_fields_project : status_fields_no_project };
}

std::string rv_editor_status_tools_text(int missing)
{
    if (missing == 0) {
        return rv_editor_text("shell_status.tools_ready");
    }
    const std::string shown = missing > status_count_cap ? std::to_string(status_count_cap) + "+" : std::to_string(missing);
    return rv_editor_text_format("shell_status.tools_missing", std::make_format_args(shown));
}

rv_editor_status_text rv_editor_status_text_widest(const rv_editor_app &app)
{
    // Fields at their limits: counts and numbers at six digits, the step name at the protocol's longest; the runtime
    // state is the widest template by pixels.
    ImFont *const font = ImGui::GetIO().Fonts->Fonts[0];
    const std::string step =
        rv_editor_text("shell_status.build_widest") + std::string(rv_pdklib::rv_progress_step_name_max, 'W');
    std::string runtime;
    float runtime_w = -1.0f;
    for (int i = 0; i <= static_cast<int>(rv_editor_run_state::refused); ++i) {
        const char *const name = rv_editor_run_state_name(static_cast<rv_editor_run_state>(i));
        std::string t =
            rv_editor_text_format("shell_status.runtime_with_number", std::make_format_args(name, status_number_limit));
        const float w = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.0f, t.c_str()).x;
        if (w > runtime_w) {
            runtime_w = w;
            runtime = std::move(t);
        }
    }
    return { rv_editor_status_where(app),
        rv_editor_status_tools_text(status_count_cap + 1),
        step,
        runtime,
        app.project.open ? status_fields_project : status_fields_no_project };
}

float rv_editor_shell_status_min_width(const rv_editor_app &app, const rv_editor_theme &theme)
{
    const rv_editor_status_text w = rv_editor_status_text_widest(app);
    const char *const widest[] = { w.where.c_str(), w.tools.c_str(), w.build.c_str(), w.runtime.c_str() };
    return rv_editor_status_bar_min_width(widest, status_fields_project, theme);
}

int rv_editor_window_min_width_for(float scale, float status_min)
{
    return static_cast<int>(std::ceil(std::max(window_min_width * scale, status_min)));
}

bool rv_editor_shell_window_min_changed(rv_editor_shell &shell, const rv_editor_theme &theme, int &min_out)
{
    min_out = rv_editor_window_min_width_for(theme.scale, rv_editor_shell_status_min_width(shell.app, theme));
    const bool changed = min_out != shell.window_min_last;
    shell.window_min_last = min_out;
    return changed;
}

void rv_editor_shell_status(rv_editor_shell &shell, const rv_editor_theme &theme, rv_editor_status_probe *probe)
{
    const rv_editor_status_text text = rv_editor_status_text_make(shell.app);
    const rv_editor_status_text widest = rv_editor_status_text_widest(shell.app);
    const char *const fields[] = { text.where.c_str(), text.tools.c_str(), text.build.c_str(), text.runtime.c_str() };
    const char *const limits[] = { widest.where.c_str(), widest.tools.c_str(), widest.build.c_str(), widest.runtime.c_str() };
    // Tools is the second segment in both layouts.
    if (rv_editor_status_bar(fields, limits, text.shown, theme, probe) == status_tools_index) {
        rv_editor_shell_page(shell, rv_editor_start_page::settings);
    }
}

} // namespace rv_editor
