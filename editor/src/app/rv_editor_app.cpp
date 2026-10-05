// What the editor's commands do to its models, and why one cannot run now.

#include "app/rv_editor_app.hpp"

#include <chrono>
#include <system_error>
#include <thread>

#include "pdk/rv_err.h"

#include "project/rv_editor_templates.hpp"
#include "release/rv_editor_candidate_store.hpp"

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

} // namespace

int rv_editor_app_start(rv_editor_app &app, const rv_editor_artifact &artifact, const std::filesystem::path &card)
{
    std::error_code ec;
    std::filesystem::create_directories(app.project.state_dir, ec);
    if (ec) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
            app.project.state_dir.string() + ": " + ec.message());
        return RV_ERR_IO;
    }
    app.build.prune(artifact.dir);
    // A new console starts with every key up: no capture carries over.
    app.game_captured = false;
    app.marks.clear();
    app.session_first_seq = app.log.revision() + 1;
    app.observe.read_frame = -1;
    // A development run follows the active profile; a candidate, with its own card, does not.
    const rv_editor_run_profile none;
    const rv_editor_run_profile &profile = card.empty() ? app.run_config.profiles[app.run_config.active] : none;
    const std::filesystem::path runtime = rv_editor_run_profile_path(profile.runtime, app.project.root);
    const std::filesystem::path own_card = rv_editor_run_profile_path(profile.memcard, app.project.root);
    const std::filesystem::path cwd = rv_editor_run_profile_path(profile.cwd, app.project.root);
    app.session_profile = card.empty() ? profile.name : "the candidate's own";
    if (card.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
            "run profile " + profile.name + ": build #" + std::to_string(artifact.number));
    }
    std::string error;
    if (!app.session.start(runtime.empty() ? app.tools.console.path : runtime, artifact.dir,
            !card.empty() ? card : !own_card.empty() ? own_card : app.project.state_dir / "memcard.mppccard",
            cwd.empty() ? app.project.root : cwd, artifact.number, rv_editor_run_profile_args(profile), profile.env,
            app.log, error)) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot start the runtime: " + error);
        return RV_ERR_IO;
    }
    const std::filesystem::path map_path = rv_editor_build_map_path(artifact.dir);
    app.build_map = rv_editor_build_map_read(map_path);
    if (app.build_map.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
            map_path.string() + ": no map; reload targets fall back to the entry script");
    }
    rv_editor_app_attach_session_log(app);
    return RV_OK;
}

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
    // Candidates, Observe's pins and findings belong to the project they came from.
    app.release = {};
    app.observe = {};
    app.findings = {};
    app.scene_tabs = {};
    // Another project takes the keyboard back from the game.
    app.game_captured = false;
    app.session.pad(0, app.log);
    app.files.open(app.project.root, app.log);
    std::vector<std::string> unread;
    app.release.candidates = rv_editor_candidates_load(app.project.cache_dir / "candidates", unread);
    for (const std::string &why : unread) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "candidate record not read: " + why);
    }
    // Their identity is checked again: the bytes may have changed while no window watched.
    for (rv_editor_candidate &c : app.release.candidates) {
        rv_editor_candidate_hash(c);
    }
    app.release.selected = app.release.candidates.empty() ? 0 : app.release.candidates.size() - 1;
    rv_editor_app_scene_first(app);
    app.run_config = rv_editor_run_config_load(app.project.root);
    if (!app.run_config.error.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, app.run_config.error);
    }
    app.run_problem = rv_editor_run_profile_problem(app.run_config.profiles[app.run_config.active], app.project.root);
    ++app.run_config_revision;
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "opened " + app.project.root.string());
    rv_editor_recent_add(app.project.root);
    if (!app.project.manifest_error.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, app.project.manifest_error);
    }
    return true;
}

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

std::vector<int64_t> rv_editor_app_unsaved(const rv_editor_app &app)
{
    std::vector<int64_t> ids;
    for (const rv_editor_nvim_buffer &b : app.nvim.modified()) {
        if (!b.name.empty()) {
            ids.push_back(b.id);
        }
    }
    return ids;
}

bool rv_editor_app_run_builds(const rv_editor_app &app)
{
    return app.inputs_changed || app.build.dev_state() != rv_editor_build_state::succeeded ||
        !app.build.last_success();
}

void rv_editor_app_build(rv_editor_app &app)
{
    if (rv_editor_app_why_not_build(app) != nullptr) {
        return;
    }
    if (!rv_editor_app_unsaved(app).empty() || rv_editor_app_scene_dirty(app)) {
        app.unsaved_ask = rv_editor_unsaved_ask::build;
        return;
    }
    rv_editor_app_build_saved(app);
}

void rv_editor_app_build_saved(rv_editor_app &app)
{
    if (rv_editor_app_why_not_build(app) != nullptr) {
        return;
    }
    std::string error;
    app.build_first_seq = app.log.revision() + 1;
    app.run_after_build = false;
    app.restart_after_build = false;
    app.restart_after_stop = false;
    if (!app.build.start(app.project, app.tools, app.log, error)) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot build: " + error);
        return;
    }
    rv_editor_app_attach_build_log(app);
    app.inputs_changed = false;
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
    if (!rv_editor_app_unsaved(app).empty() || rv_editor_app_scene_dirty(app)) {
        app.unsaved_ask = rv_editor_unsaved_ask::run;
        return;
    }
    rv_editor_app_run_saved(app);
}

void rv_editor_app_run_saved(rv_editor_app &app)
{
    if (rv_editor_app_why_not_run(app) != nullptr) {
        return;
    }
    app.restart_after_build = false;
    app.restart_after_stop = false;
    if (!rv_editor_app_run_builds(app)) {
        (void)rv_editor_app_start(app, *app.build.last_success());
        return;
    }
    rv_editor_app_build_saved(app);
    app.run_after_build = app.build.busy();
}

void rv_editor_app_profiles_save(rv_editor_app &app)
{
    std::string error;
    if (!rv_editor_run_config_save(app.project.root, app.run_config, error)) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "run profiles not saved: " + error);
    }
    app.run_config.error.clear();
    app.run_problem = rv_editor_run_profile_problem(app.run_config.profiles[app.run_config.active], app.project.root);
    ++app.run_config_revision;
}

void rv_editor_app_run_last(rv_editor_app &app)
{
    if (rv_editor_app_why_not_run_last(app) != nullptr) {
        return;
    }
    const rv_editor_artifact artifact = *app.build.last_success();
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
        "running the last successful build, #" + std::to_string(artifact.number) +
            ": its native code with the current scripts, assets and scenes");
    (void)rv_editor_app_start(app, artifact);
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

int rv_editor_app_rename(rv_editor_app &app, const std::filesystem::path &from, const std::string &name,
    std::string &error)
{
    error = app.nvim.buffers_unsaved_at(from);
    if (!error.empty()) {
        return RV_ERR_BUSY;
    }
    const std::filesystem::path to = from.parent_path() / name;
    if (!app.files.rename(from, name, error)) {
        return RV_ERR_INVAL;
    }
    app.nvim.rename_buffers_at(from, to);
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
        "renamed " + from.string() + " to " + name);
    return RV_OK;
}

int rv_editor_app_remove(rv_editor_app &app, const std::filesystem::path &path, std::string &error)
{
    error = app.nvim.buffers_unsaved_at(path);
    if (!error.empty()) {
        return RV_ERR_BUSY;
    }
    if (!app.files.remove(path, error)) {
        return RV_ERR_INVAL;
    }
    app.nvim.delete_buffers_at(path);
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "deleted " + path.string());
    return RV_OK;
}

void rv_editor_app_update(rv_editor_app &app)
{
    app.files.update(app.log);
    app.nvim.update(app.log);
    // Every shell reads on, behind another tab too, so none stalls on a full terminal.
    for (auto &[pane, view] : app.terminals) {
        if (view.term != nullptr) {
            view.term->update();
        }
    }
    if (!app.files.changed.empty()) {
        // Clean buffers follow the disk; nvim asks about modified ones.
        app.nvim.checktime();
    }
    if (!app.files.changed.empty()) {
        rv_editor_app_release_changed(app);
        app.inputs_changed = true;
    }
    for (const std::filesystem::path &changed : app.files.changed) {
        if (app.run_config.profiles[app.run_config.active].reload_on_save) {
            rv_editor_app_reload_queue_note(app, changed);
        }
        if (app.project.open && changed == app.project.manifest) {
            // disc.toml is the source of truth: what the Project pane
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
        app.build_ended = std::filesystem::file_time_type::clock::now();
        if (app.run_after_build) {
            // The build Run started runs, or Run says why not; never the one before.
            app.run_after_build = false;
            if (app.build.dev_state() == rv_editor_build_state::succeeded && app.build.last_success()) {
                (void)rv_editor_app_start(app, *app.build.last_success());
            } else {
                app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
                    std::string("not run: the build ") + rv_editor_build_state_name(app.build.dev_state()));
            }
        }
    }
    // The latest job's places, once per change of the log.
    if (app.problems_revision != app.log.revision()) {
        app.problems_revision = app.log.revision();
        app.problems.clear();
        for (const rv_editor_log_line &line : app.log.lines()) {
            rv_editor_problem p;
            if (line.seq >= app.build_first_seq &&
                (line.source == rv_editor_log_source::build || line.source == rv_editor_log_source::candidate) &&
                rv_editor_problem_parse(line.text, app.project.root, p)) {
                app.problems.push_back(std::move(p));
            }
        }
    }
    rv_editor_app_release_update(app, was_busy && !app.build.busy());
    if (app.run_after_stop && !app.session.live()) {
        app.run_after_stop = false;
        rv_editor_app_run(app);
    }
    rv_editor_app_build_restart_update(app, was_busy && !app.build.busy());
    rv_editor_app_reload_queue_update(app);
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
    app.nvim.stop();
    app.terminals.clear(); // each shell hangs up and is reaped
}

} // namespace rv_editor
