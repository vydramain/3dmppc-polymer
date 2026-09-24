// What the editor's commands do to its models, and why one cannot run now.

#include "app/rv_editor_app.hpp"

#include <chrono>
#include <system_error>
#include <thread>

namespace rv_editor
{

namespace
{

void rv_editor_app_tool_note(rv_editor_app &app, const char *name, const rv_editor_tool &tool)
{
    if (tool.problem.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
            std::string(name) + ": " + tool.path.string() + " (" + tool.origin + ")" +
                (tool.version.empty() ? "" : ", " + tool.version));
        return;
    }
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string(name) + ": " + tool.problem);
}

bool rv_editor_app_start(rv_editor_app &app, const rv_editor_artifact &artifact)
{
    std::error_code ec;
    std::filesystem::create_directories(app.project.state_dir, ec);
    if (ec) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
            app.project.state_dir.string() + ": " + ec.message());
        return false;
    }
    app.build.prune(artifact.dir);
    std::string error;
    if (!app.session.start(app.tools.console.path, artifact.dir, app.project.state_dir / "memcard.mppccard",
            app.project.root, artifact.number, app.log, error)) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot start the runtime: " + error);
        return false;
    }
    return true;
}

} // namespace

void rv_editor_app_init(rv_editor_app &app)
{
    app.tools = rv_editor_toolchain_find();
    if (!app.tools.settings_error.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, app.tools.settings_error);
    }
    rv_editor_app_tool_note(app, "runtime", app.tools.console);
    rv_editor_app_tool_note(app, "burner", app.tools.burner);
    rv_editor_app_tool_note(app, "baker", app.tools.baker);
}

bool rv_editor_app_open(rv_editor_app &app, const std::filesystem::path &target)
{
    std::string error;
    rv_editor_project project;
    if (!rv_editor_project_open(target, project, error)) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot open: " + error);
        return false;
    }
    app.project = std::move(project);
    app.files.open(app.project.root, app.log);
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "opened " + app.project.root.string());
    if (!app.project.manifest_error.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, app.project.manifest_error);
    }
    return true;
}

const char *rv_editor_app_why_not_build(const rv_editor_app &app)
{
    if (!app.project.open) {
        return "No project is open: File > Open Folder";
    }
    if (app.build.busy()) {
        return "A build is already running";
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
        return "No project is open: File > Open Folder";
    }
    if (!app.tools.console.problem.empty()) {
        return app.tools.console.problem.c_str();
    }
    if (app.project.state_dir.empty()) {
        return "No place for the memory card: neither XDG_STATE_HOME nor HOME is set";
    }
    if (app.build.busy()) {
        return "Waiting for the build to finish";
    }
    switch (app.build.state()) {
        case rv_editor_build_state::idle: return "Nothing built yet: Build first";
        case rv_editor_build_state::failed:
            return "The last build failed; Run > Run Last Successful Build starts the one before";
        case rv_editor_build_state::cancelled:
            return "The last build was cancelled; Run > Run Last Successful Build starts the one before";
        default: break;
    }
    return nullptr;
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

void rv_editor_app_build(rv_editor_app &app)
{
    if (rv_editor_app_why_not_build(app) != nullptr) {
        return;
    }
    std::string error;
    if (!app.build.start(app.project, app.tools, app.log, error)) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot build: " + error);
    }
}

void rv_editor_app_run(rv_editor_app &app)
{
    if (rv_editor_app_why_not_run(app) != nullptr) {
        return;
    }
    if (app.session.state() == rv_editor_run_state::paused) {
        app.session.resume(app.log);
        return;
    }
    rv_editor_app_start(app, *app.build.last_success());
}

void rv_editor_app_run_last(rv_editor_app &app)
{
    if (rv_editor_app_why_not_run_last(app) != nullptr) {
        return;
    }
    const rv_editor_artifact artifact = *app.build.last_success();
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
        "running the last successful build, #" + std::to_string(artifact.number) + ", not the latest build");
    rv_editor_app_start(app, artifact);
}

void rv_editor_app_pause(rv_editor_app &app)
{
    app.session.pause(app.log);
}

void rv_editor_app_step(rv_editor_app &app)
{
    app.session.step(app.log);
}

void rv_editor_app_stop(rv_editor_app &app)
{
    app.session.stop(app.log);
}

bool rv_editor_app_rename(rv_editor_app &app, const std::filesystem::path &from, const std::string &name,
    std::string &error)
{
    if (!app.files.rename(from, name, error)) {
        return false;
    }
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
        "renamed " + from.string() + " to " + name);
    return true;
}

bool rv_editor_app_remove(rv_editor_app &app, const std::filesystem::path &path, std::string &error)
{
    if (!app.files.remove(path, error)) {
        return false;
    }
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "deleted " + path.string());
    return true;
}

void rv_editor_app_update(rv_editor_app &app)
{
    app.files.update(app.log);
    for (const std::filesystem::path &changed : app.files.changed) {
        if (app.project.open && changed == app.project.manifest) {
            // disc.toml is the source of truth (PRJ-03): what the Project pane
            // shows follows it; a running console keeps what it loaded.
            rv_editor_project_reload_manifest(app.project);
        }
    }
    app.files.changed.clear();
    const bool was_busy = app.build.busy();
    app.build.update(app.log);
    app.session.update(app.log);
    if (was_busy && !app.build.busy()) {
        app.build.prune(app.session.live() ? app.session.disc_dir() : std::filesystem::path());
    }
}

void rv_editor_app_shutdown(rv_editor_app &app)
{
    app.build.cancel();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (app.build.busy() && std::chrono::steady_clock::now() < until) {
        app.build.update(app.log);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    app.session.shutdown(app.log);
}

} // namespace rv_editor
