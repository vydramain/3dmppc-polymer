// The change classifier against the running build's map: Reload's target, Build
// and Restart, and Reload On Save's queue.

#include "app/rv_editor_app.hpp"

#include <algorithm>
#include <chrono>

#include "pdk/rv_err.h"

namespace rv_editor
{

rv_editor_change_plan rv_editor_app_change_for(const rv_editor_app &app, const std::filesystem::path &file)
{
    static const std::map<std::string, rv_editor_map_entry> empty;
    return rv_editor_change_plan_for(app.project.root, app.project.manifest, app.session.live() ? app.build_map : empty, file);
}

namespace
{

// Quiet time after the last file change before a reload.
constexpr auto reload_delay = std::chrono::milliseconds(300);

// Sends the reload for `file`'s plan (reload_module: that module; otherwise the
// entry); an empty `file` always means the entry. The caller has already gated.
void rv_editor_app_reload_send(rv_editor_app &app, const std::filesystem::path &file)
{
    // A new reload supersedes the last bake's outcome in the status row.
    app.texture_bake.message.clear();
    const rv_editor_change_plan plan = file.empty() ? rv_editor_change_plan() : rv_editor_app_change_for(app, file);
    if (plan.action == rv_editor_change_action::refresh_texture) {
        rv_editor_app_texture_bake_start(app, plan.name, file);
        return;
    }
    const bool by_module = plan.action == rv_editor_change_action::reload_module;
    const std::string module = by_module ? plan.name : std::string();
    std::string error;
    const int reload_result = app.session.reload(app.log, error, module);
    if (reload_result != RV_OK) {
        // Busy is a refusal to retry later (not ready, a reload in flight): a warning.
        const rv_editor_log_level level =
            reload_result == RV_ERR_BUSY ? rv_editor_log_level::warning : rv_editor_log_level::error;
        const std::string what =
            module.empty() ? std::string("cannot reload the entry: ") : "cannot reload module " + module + ": ";
        app.log.add(rv_editor_log_source::editor, level, what + error);
    }
}

} // namespace

void rv_editor_app_reload(rv_editor_app &app)
{
    if (!rv_editor_app_can_reload_now(app)) {
        return;
    }
    rv_editor_app_reload_send(app, app.code_file);
}

void rv_editor_app_reload_queue_note(rv_editor_app &app, const std::filesystem::path &changed)
{
    const rv_editor_change_plan plan = rv_editor_app_change_for(app, changed);
    if (plan.action != rv_editor_change_action::reload_entry && plan.action != rv_editor_change_action::reload_module &&
        plan.action != rv_editor_change_action::refresh_texture) {
        return;
    }
    if (std::find(app.reload_queue.begin(), app.reload_queue.end(), changed) == app.reload_queue.end()) {
        app.reload_queue.push_back(changed);
    }
    // Every relevant change moves the settle wait on, for the whole burst.
    app.reload_due = std::chrono::steady_clock::now() + reload_delay;
}

// A queue left over from a session that ended is dropped; nothing can refuse it, so it returns nothing.
void rv_editor_app_reload_queue_drop(rv_editor_app &app)
{
    app.reload_queue.clear();
    app.reload_due = {};
}

// Zero reload_due means none is due.
bool rv_editor_app_reload_due(const rv_editor_app &app)
{
    return app.reload_due != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() >= app.reload_due;
}

// Refusals (not running or paused, a packed medium, a reload or bake already out) keep the queue whole.
bool rv_editor_app_can_reload_now(const rv_editor_app &app)
{
    return rv_editor_app_can_reload(app) && rv_editor_app_why_not_reload(app) == nullptr &&
        !rv_editor_app_texture_bake_busy(app);
}

int rv_editor_app_reload_queue_send(rv_editor_app &app)
{
    if (app.reload_queue.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "reload queue is empty: nothing to send");
        return RV_ERR_INVAL;
    }
    if (!rv_editor_app_can_reload_now(app)) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "reload waits: the session cannot take it now");
        return RV_ERR_BUSY;
    }
    const std::filesystem::path file = app.reload_queue.front();
    app.reload_queue.erase(app.reload_queue.begin());
    if (app.reload_queue.empty()) {
        app.reload_due = {};
    }
    rv_editor_app_reload_send(app, file);
    return RV_OK;
}

int rv_editor_app_changed_files_apply(rv_editor_app &app)
{
    if (app.files.changed.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "changed files: none to apply");
        return RV_ERR_INVAL;
    }
    // Clean buffers follow the disk; nvim asks about modified ones.
    app.nvim.checktime();
    rv_editor_app_release_changed(app);
    app.inputs_changed = true;
    const bool reload_on_save = app.run_config.profiles[app.run_config.active].reload_on_save;
    for (const std::filesystem::path &changed : app.files.changed) {
        rv_editor_app_descriptions_file_changed(app, changed);
        if (reload_on_save) {
            rv_editor_app_reload_queue_note(app, changed);
        }
        // disc.toml is the source of truth: what the Project pane
        // shows follows it; a running console keeps what it loaded.
        if (app.project.open && changed == app.project.manifest) {
            rv_editor_project_reload_manifest(app.project);
        }
    }
    app.files.changed.clear();
    return RV_OK;
}

int rv_editor_app_build_restart(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_build(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not built: ") + why);
        return RV_ERR_BUSY;
    }
    if (!rv_editor_app_unsaved(app).empty() || rv_editor_app_scene_dirty(app)) {
        app.unsaved_ask = rv_editor_unsaved_ask::build_restart;
        return RV_ERR_PENDING;
    }
    return rv_editor_app_build_restart_saved(app);
}

int rv_editor_app_build_restart_saved(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_build(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not built: ") + why);
        return RV_ERR_BUSY;
    }
    const int err = rv_editor_app_build_saved(app);
    app.restart_after_build = app.build.busy();
    return err;
}

bool rv_editor_app_build_restart_built(const rv_editor_app &app)
{
    return app.build.dev_state() == rv_editor_build_state::succeeded && app.build.last_success();
}

int rv_editor_app_build_restart_refuse(rv_editor_app &app)
{
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::warning,
        std::string("the game was not restarted: the build ") + rv_editor_build_state_name(app.build.dev_state()));
    return RV_ERR_BUSY;
}

int rv_editor_app_build_restart_stop(rv_editor_app &app)
{
    app.restart_after_stop = true;
    std::string error;
    const int stop_result = app.session.stop(app.log, error);
    // RV_ERR_ALREADY: a stop is under way or the runtime is gone; the restart goes on.
    const bool stop_failed = stop_result != RV_OK && stop_result != RV_ERR_ALREADY;
    if (stop_failed) {
        app.restart_after_stop = false;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot stop the runtime: " + error);
        return stop_result;
    }
    return RV_OK;
}

int rv_editor_app_build_restart_start(rv_editor_app &app)
{
    if (!app.build.last_success()) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "the game was not restarted: no successful build to run");
        return RV_ERR_NOENT;
    }
    return rv_editor_app_run_session(app, *app.build.last_success());
}

} // namespace rv_editor
