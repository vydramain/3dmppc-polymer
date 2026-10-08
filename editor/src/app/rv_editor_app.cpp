// What the editor's commands do to its models, and why one cannot run now.

#include "app/rv_editor_app.hpp"

#include <chrono>
#include <thread>

#include "pdk/rv_err.h"

#include "project/rv_editor_templates.hpp"
#include "release/rv_editor_candidate_store.hpp"

namespace rv_editor
{

namespace
{

constexpr auto shutdown_wait_timeout = std::chrono::seconds(4);
constexpr auto shutdown_poll_interval = std::chrono::milliseconds(20);
// Cache subdirectory holding release candidates and their logs.
constexpr std::string_view candidates_dir_name = "candidates";

void rv_editor_app_tool_note(rv_editor_app &app, const char *name, const rv_editor_tool &tool)
{
    const bool ok = tool.problem.empty();
    const rv_editor_log_level level = ok ? rv_editor_log_level::info : rv_editor_log_level::warning;
    const std::string version_suffix = ok && !tool.version.empty() ? ", " + tool.version : "";
    const std::string detail = ok ? tool.path.string() + " (" + tool.origin + ")" + version_suffix : tool.problem;
    app.log.add(rv_editor_log_source::editor, level, std::string(name) + ": " + detail);
}

} // namespace

// void: nothing here can fail with an RV code. A missing or broken tool or settings file
// is a warning in app.log, and the editor goes on with the defaults.
void rv_editor_app_init(rv_editor_app &app)
{
    app.tools = rv_editor_toolchain_find();
    if (!app.tools.settings_error.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, app.tools.settings_error);
    }
    rv_editor_app_tool_note(app, "runtime", app.tools.console);
    rv_editor_app_tool_note(app, "burner", app.tools.burner);
    rv_editor_app_tool_note(app, "baker", app.tools.baker);
}

int rv_editor_app_open(rv_editor_app &app, const std::filesystem::path &target)
{
    std::string error;
    rv_editor_project project;
    const int err = rv_editor_project_open(target, project, error);
    if (err != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot open: " + error);
        return err;
    }
    app.project = std::move(project);
    // Candidates, Observe's pins and findings belong to the project they came from.
    app.release = {};
    app.observe = {};
    app.findings = {};
    app.scene_tabs = {};
    // Another project takes the keyboard back from the game.
    app.game_captured = false;
    if (app.session.pad_pending(0)) {
        std::string pad_error;
        if (app.session.pad(0, app.log, pad_error) != RV_OK) {
            app.log.add(rv_editor_log_source::editor,
                rv_editor_log_level::warning,
                "cannot release the game's pad: " + pad_error);
        }
    }
    std::string files_error;
    const int files_rc = app.files.open(app.project.root, files_error);
    if (files_rc != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "project files: " + files_error);
    }
    std::string watch_error;
    const int watch_rc = files_rc == RV_OK ? app.files.start_watching(watch_error) : RV_OK;
    if (watch_rc == RV_ERR_NOMEM) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            "file watch: watch limit reached, part of the tree is not watched: " + watch_error);
    } else if (watch_rc != RV_OK) {
        if (!app.files.watching()) {
            watch_error += "; files are not watched, Refresh shows changes";
        }
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "file watch: " + watch_error);
    }
    std::vector<std::string> unread;
    app.release.candidates = rv_editor_candidates_load(app.project.cache_dir / std::string(candidates_dir_name), unread);
    for (const std::string &why : unread) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "candidate record not read: " + why);
    }
    // Their identity is checked again: the bytes may have changed while no window watched.
    for (rv_editor_candidate &c : app.release.candidates) {
        rv_editor_candidate_hash(c);
    }
    app.release.selected = app.release.candidates.empty() ? 0 : app.release.candidates.size() - 1;
    rv_editor_app_scene_first(app);
    app.run_config = rv_editor_run_config_load(app.project.root);
    if (!app.run_config.error.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, app.run_config.error);
    }
    app.run_problem = rv_editor_run_profile_problem(app.run_config.profiles[app.run_config.active], app.project.root);
    ++app.run_config_revision;
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "opened " + app.project.root.string());
    rv_editor_recent_add(app.project.root);
    if (!app.project.manifest_error.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, app.project.manifest_error);
    }
    return RV_OK;
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
    return app.inputs_changed || app.build.dev_state() != rv_editor_build_state::succeeded || !app.build.last_success();
}

int rv_editor_app_build(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_build(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not built: ") + why);
        return RV_ERR_BUSY;
    }
    if (!rv_editor_app_unsaved(app).empty() || rv_editor_app_scene_dirty(app)) {
        app.unsaved_ask = rv_editor_unsaved_ask::build;
        return RV_ERR_BUSY;
    }
    return rv_editor_app_build_saved(app);
}

int rv_editor_app_build_saved(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_build(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not built: ") + why);
        return RV_ERR_BUSY;
    }
    std::string error;
    app.build_first_seq = app.log.revision() + 1;
    app.run_after_build = false;
    app.restart_after_build = false;
    app.restart_after_stop = false;
    const int err = app.build.start(app.project, app.tools, app.log, error);
    if (err != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot build: " + error);
        return err;
    }
    rv_editor_app_attach_build_log(app);
    app.inputs_changed = false;
    return RV_OK;
}

int rv_editor_app_run(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_run(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not run: ") + why);
        return RV_ERR_BUSY;
    }
    if (app.session.state() == rv_editor_run_state::paused) {
        std::string error;
        const int err = app.session.resume(app.log, error);
        if (err != RV_OK) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "cannot resume: " + error);
        }
        return err;
    }
    if (!rv_editor_app_unsaved(app).empty() || rv_editor_app_scene_dirty(app)) {
        app.unsaved_ask = rv_editor_unsaved_ask::run;
        return RV_ERR_BUSY;
    }
    return rv_editor_app_run_saved(app);
}

int rv_editor_app_run_saved(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_run(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not run: ") + why);
        return RV_ERR_BUSY;
    }
    app.restart_after_build = false;
    app.restart_after_stop = false;
    if (!rv_editor_app_run_builds(app)) {
        return rv_editor_app_run_session(app, *app.build.last_success());
    }
    const int err = rv_editor_app_build_saved(app);
    app.run_after_build = app.build.busy();
    return err;
}

int rv_editor_app_profiles_save(rv_editor_app &app)
{
    app.run_config.error.clear();
    app.run_problem = rv_editor_run_profile_problem(app.run_config.profiles[app.run_config.active], app.project.root);
    ++app.run_config_revision;
    std::string error;
    const int err = rv_editor_run_config_save(app.project.root, app.run_config, error);
    if (err != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "run profiles not saved: " + error);
        return err;
    }
    return RV_OK;
}

int rv_editor_app_run_last(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_run_last(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not run: ") + why);
        return RV_ERR_BUSY;
    }
    const rv_editor_artifact artifact = *app.build.last_success();
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::warning,
        "running the last successful build, #" + std::to_string(artifact.number) +
            ": its native code with the current scripts, assets and scenes");
    return rv_editor_app_run_session(app, artifact);
}

int rv_editor_app_pause(rv_editor_app &app)
{
    std::string error;
    const int err = app.session.pause(app.log, error);
    if (err != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "cannot pause: " + error);
    }
    return err;
}

int rv_editor_app_step(rv_editor_app &app)
{
    std::string error;
    const int err = app.session.step(app.log, error);
    if (err != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "cannot step: " + error);
    }
    return err;
}

// Logs a stop operation's outcome: ALREADY is a warning, a refusal an error with `prefix`.
// A refused stop also cancels a Run queued behind it.
static void report_stop(rv_editor_app &app, int code, const std::string &error, const std::string &prefix)
{
    if (code == RV_ERR_ALREADY) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, error);
        return;
    }
    if (code != RV_OK) {
        app.run_after_stop = false;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, prefix + error);
    }
}

int rv_editor_app_stop(rv_editor_app &app)
{
    std::string error;
    const int err = app.session.stop(app.log, error);
    report_stop(app, err, error, "cannot stop the runtime: ");
    return err;
}

int rv_editor_app_force_stop(rv_editor_app &app)
{
    std::string error;
    const int err = app.session.force_stop(app.log, error);
    report_stop(app, err, error, "cannot force-stop the runtime: ");
    return err;
}

int rv_editor_app_rename(rv_editor_app &app, const std::filesystem::path &from, const std::string &name, std::string &error)
{
    error = app.nvim.buffers_unsaved_at(from);
    if (!error.empty()) {
        return RV_ERR_BUSY;
    }
    const std::filesystem::path to = from.parent_path() / name;
    const int err = app.files.rename(from, name, error);
    if (err != RV_OK) {
        return err;
    }
    app.nvim.rename_buffers_at(from, to);
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "renamed " + from.string() + " to " + name);
    return RV_OK;
}

int rv_editor_app_remove(rv_editor_app &app, const std::filesystem::path &path, std::string &error)
{
    error = app.nvim.buffers_unsaved_at(path);
    if (!error.empty()) {
        return RV_ERR_BUSY;
    }
    const int err = app.files.remove(path, error);
    if (err != RV_OK) {
        return err;
    }
    app.nvim.delete_buffers_at(path);
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "deleted " + path.string());
    return RV_OK;
}

void rv_editor_app_shutdown(rv_editor_app &app)
{
    app.build.cancel();
    const auto until = std::chrono::steady_clock::now() + shutdown_wait_timeout;
    while (app.build.busy() && std::chrono::steady_clock::now() < until) {
        std::string build_error;
        const int build_result = app.build.update(app.log, build_error);
        if (build_result != RV_OK) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "burner: " + build_error);
        }
        std::this_thread::sleep_for(shutdown_poll_interval);
    }
    std::string session_error;
    const int session_result = app.session.shutdown(app.log, session_error);
    if (session_result != RV_OK) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "runtime did not end cleanly at shutdown: " + session_error);
    }
    std::string nvim_error;
    if (app.nvim.stop(nvim_error) != RV_OK) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "code editor did not end cleanly at shutdown: " + nvim_error);
    }
    // Every shell ends before the editor does: a background job that ignores the hang-up
    // would outlive the editor, as the guard only signals its own group. All are hung up
    // first, so the waits overlap and the exit takes one deadline, not one per shell.
    for (auto &entry : app.terminals) {
        if (entry.second.term != nullptr) {
            app.stopping_ptys.push_back(entry.second.term->release_pty());
        }
    }
    for (std::unique_ptr<rv_editor_pty> &pty : app.stopping_ptys) {
        std::string error;
        if (pty->begin_stop(error) != RV_OK) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "terminal: " + error);
        }
    }
    for (std::unique_ptr<rv_editor_pty> &pty : app.stopping_ptys) {
        std::string error;
        if (pty->stop_now(error) != RV_OK) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "terminal: " + error);
        }
    }
    app.terminals.clear();     // idle terminals now
    app.stopping_ptys.clear(); // ended above
}

} // namespace rv_editor
