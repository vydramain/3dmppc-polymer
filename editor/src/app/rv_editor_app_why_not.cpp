// Why each editor command cannot run now.

#include "app/rv_editor_app.hpp"

namespace rv_editor
{

const char *rv_editor_app_why_not_build(const rv_editor_app &app)
{
    if (!app.project.open) {
        return "No project is open: File > Open Project...";
    }
    if (app.build.busy()) {
        return "A build is already running";
    }
    if (app.release.building) {
        return "Waiting for the candidate's source revision";
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
        return s == rv_editor_run_state::running ? "The runtime is already running" : "Waiting for the runtime";
    }
    if (!app.project.open) {
        return "No project is open: File > Open Project...";
    }
    if (!app.run_problem.empty()) {
        return app.run_problem.c_str();
    }
    // A profile's own runtime stands in for Settings' one.
    if (app.run_config.profiles[app.run_config.active].runtime.empty() && !app.tools.console.problem.empty()) {
        return app.tools.console.problem.c_str();
    }
    if (app.project.state_dir.empty()) {
        return "No place for the memory card: neither XDG_STATE_HOME nor HOME is set";
    }
    if (app.build.busy()) {
        return "Waiting for the build to finish";
    }
    // Run builds first when it has to, so what stops a build stops it.
    return rv_editor_app_run_builds(app) ? rv_editor_app_why_not_build(app) : nullptr;
}

const char *rv_editor_app_why_not_run_last(const rv_editor_app &app)
{
    if (app.session.live()) {
        return "The runtime is already running";
    }
    if (!app.tools.console.problem.empty()) {
        return app.tools.console.problem.c_str();
    }
    if (app.build.busy()) {
        return "Waiting for the build to finish";
    }
    if (!app.build.last_success()) {
        return "No build has succeeded in this window";
    }
    return nullptr;
}

const char *rv_editor_app_why_not_pause(const rv_editor_app &app)
{
    switch (app.session.state()) {
        case rv_editor_run_state::running: return nullptr;
        case rv_editor_run_state::paused: return "Already paused";
        case rv_editor_run_state::pausing:
        case rv_editor_run_state::stepping:
        case rv_editor_run_state::resuming:
        case rv_editor_run_state::starting: return "Waiting for the runtime to answer";
        default: return "No runtime is running";
    }
}

const char *rv_editor_app_why_not_step(const rv_editor_app &app)
{
    switch (app.session.state()) {
        case rv_editor_run_state::paused: return nullptr;
        case rv_editor_run_state::running: return "Pause first: Step runs one frame of a paused machine";
        case rv_editor_run_state::pausing:
        case rv_editor_run_state::stepping:
        case rv_editor_run_state::resuming:
        case rv_editor_run_state::starting: return "Waiting for the runtime to answer";
        default: return "No runtime is running";
    }
}

const char *rv_editor_app_why_not_stop(const rv_editor_app &app)
{
    if (!app.session.live()) {
        return "No runtime is running";
    }
    if (app.session.state() == rv_editor_run_state::stopping) {
        return "Stopping; Force Stop ends it if it hangs";
    }
    return nullptr;
}

bool rv_editor_app_can_reload(const rv_editor_app &app)
{
    const rv_editor_session_facts &f = app.session.facts();
    return app.session.connected() && f.medium == "live" && f.reloadable;
}

const char *rv_editor_app_why_not_reload(const rv_editor_app &app)
{
    std::string baking;
    if (rv_editor_app_texture_bake_busy(app, &baking)) {
        static std::string reason;
        reason = "Baking texture " + baking;
        return reason.c_str();
    }
    if (app.session.reloading()) {
        return "Waiting for the last reload's answer";
    }
    const rv_editor_run_state s = app.session.state();
    if (s != rv_editor_run_state::running && s != rv_editor_run_state::paused) {
        return "Waiting for the runtime to answer";
    }
    return nullptr;
}

} // namespace rv_editor
