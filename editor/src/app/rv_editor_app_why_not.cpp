// Why each editor command cannot run now.

#include "app/rv_editor_app.hpp"
#include "text/rv_editor_text.hpp"

namespace rv_editor
{

const char *rv_editor_app_why_not_build(const rv_editor_app &app)
{
    if (!app.project.open) {
        return rv_editor_text("pane_files.no_project_open");
    }
    if (app.build.busy()) {
        return rv_editor_text("app_why_not.build_already_running");
    }
    if (app.release.building) {
        return rv_editor_text("app_why_not.waiting_for_source");
    }
    if (!app.tools.burner.problem.empty()) {
        return app.tools.burner.problem.c_str();
    }
    if (!app.tools.baker.problem.empty()) {
        return app.tools.baker.problem.c_str();
    }
    return nullptr;
}

const char *rv_editor_app_why_not_run(const rv_editor_app &app)
{
    const rv_editor_run_state s = app.session.state();
    if (s == rv_editor_run_state::paused) {
        return nullptr; // Run resumes
    }
    if (app.session.live()) {
        return s == rv_editor_run_state::running ? rv_editor_text("app_why_not.runtime_already_running") :
                                                   rv_editor_text("app_why_not.waiting_for_runtime");
    }
    if (!app.project.open) {
        return rv_editor_text("pane_files.no_project_open");
    }
    if (!app.run_problem.empty()) {
        return app.run_problem.c_str();
    }
    // A profile's own runtime stands in for Settings' one.
    if (app.run_config.profiles[app.run_config.active].runtime.empty() && !app.tools.console.problem.empty()) {
        return app.tools.console.problem.c_str();
    }
    if (app.project.state_dir.empty()) {
        return rv_editor_text("app_why_not.no_memory_card_dir");
    }
    if (app.build.busy()) {
        return rv_editor_text("app_why_not.waiting_for_build");
    }
    // Run builds first when it has to, so what stops a build stops it.
    return rv_editor_app_run_builds(app) ? rv_editor_app_why_not_build(app) : nullptr;
}

const char *rv_editor_app_why_not_run_last(const rv_editor_app &app)
{
    if (app.session.live()) {
        return rv_editor_text("app_why_not.runtime_already_running");
    }
    if (!app.tools.console.problem.empty()) {
        return app.tools.console.problem.c_str();
    }
    if (app.build.busy()) {
        return rv_editor_text("app_why_not.waiting_for_build");
    }
    if (!app.build.last_success()) {
        return rv_editor_text("app_why_not.no_build_succeeded");
    }
    return nullptr;
}

const char *rv_editor_app_why_not_pause(const rv_editor_app &app)
{
    switch (app.session.state()) {
    case rv_editor_run_state::running:
        return nullptr;
    case rv_editor_run_state::paused:
        return rv_editor_text("app_why_not.already_paused");
    case rv_editor_run_state::pausing:
    case rv_editor_run_state::stepping:
    case rv_editor_run_state::resuming:
    case rv_editor_run_state::starting:
        return rv_editor_text("app_why_not.waiting_for_runtime_answer");
    default:
        return rv_editor_text("shell_menu.why_no_runtime");
    }
}

const char *rv_editor_app_why_not_step(const rv_editor_app &app)
{
    switch (app.session.state()) {
    case rv_editor_run_state::paused:
        return nullptr;
    case rv_editor_run_state::running:
        return rv_editor_text("app_why_not.pause_before_step");
    case rv_editor_run_state::pausing:
    case rv_editor_run_state::stepping:
    case rv_editor_run_state::resuming:
    case rv_editor_run_state::starting:
        return rv_editor_text("app_why_not.waiting_for_runtime_answer");
    default:
        return rv_editor_text("shell_menu.why_no_runtime");
    }
}

const char *rv_editor_app_why_not_stop(const rv_editor_app &app)
{
    if (!app.session.live()) {
        return rv_editor_text("shell_menu.why_no_runtime");
    }
    if (app.session.state() == rv_editor_run_state::stopping) {
        return rv_editor_text("app_why_not.stopping_runtime");
    }
    return nullptr;
}

bool rv_editor_app_can_reload(const rv_editor_app &app)
{
    const rv_editor_session_facts &f = app.session.facts();
    return app.session.connected() && f.medium == medium_live && f.reloadable;
}

const char *rv_editor_app_why_not_reload(const rv_editor_app &app)
{
    std::string baking;
    if (rv_editor_app_texture_bake_busy(app, &baking)) {
        static std::string reason;
        reason = rv_editor_text_format("app_why_not.baking_texture",
            std::make_format_args(baking));
        return reason.c_str();
    }
    if (app.session.reloading()) {
        return rv_editor_text("app_why_not.waiting_for_reload");
    }
    const rv_editor_run_state s = app.session.state();
    if (s != rv_editor_run_state::running && s != rv_editor_run_state::paused) {
        return rv_editor_text("app_why_not.waiting_for_runtime_answer");
    }
    return nullptr;
}

} // namespace rv_editor
