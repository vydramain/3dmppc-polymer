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

// Build for Build and Restart completed: stop the live session or start the new one.
void rv_editor_app_build_restart_built(rv_editor_app &app)
{
    if (!app.restart_after_build) {
        return;
    }
    app.restart_after_build = false;
    const bool built = app.build.dev_state() == rv_editor_build_state::succeeded && app.build.last_success();
    if (!built) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            std::string("the game was not restarted: the build ") + rv_editor_build_state_name(app.build.dev_state()));
        return;
    }
    if (app.session.live()) {
        app.restart_after_stop = true;
        std::string error;
        // RV_ERR_ALREADY: a stop is under way or the runtime is gone; the restart goes on.
        const int stop_result = app.session.stop(app.log, error);
        if (stop_result != RV_OK && stop_result != RV_ERR_ALREADY) {
            app.restart_after_stop = false;
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot stop the runtime: " + error);
        }
        return;
    }
    (void)rv_editor_app_run_session(app, *app.build.last_success());
}

// Restart the game after the session stopped.
void rv_editor_app_build_restart_stopped(rv_editor_app &app)
{
    if (!app.restart_after_stop || app.session.live()) {
        return;
    }
    app.restart_after_stop = false;
    (void)rv_editor_app_run_session(app, *app.build.last_success());
}

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
    if (!rv_editor_app_can_reload(app) || rv_editor_app_why_not_reload(app) != nullptr ||
        rv_editor_app_texture_bake_busy(app)) {
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

void rv_editor_app_reload_queue_update(rv_editor_app &app)
{
    // No live session to receive it: drop a queue left over from the one before.
    if (!app.session.live()) {
        app.reload_queue.clear();
        app.reload_due = {};
        return;
    }
    if (app.reload_queue.empty()) {
        return;
    }
    if (app.reload_due == std::chrono::steady_clock::time_point{} || std::chrono::steady_clock::now() < app.reload_due) {
        return;
    }
    // The gate refuses (not running/paused, a packed medium, a reload or bake
    // already out): the queue waits whole, nothing is dropped.
    if (!rv_editor_app_can_reload(app) || rv_editor_app_why_not_reload(app) != nullptr ||
        rv_editor_app_texture_bake_busy(app)) {
        return;
    }
    const std::filesystem::path file = app.reload_queue.front();
    app.reload_queue.erase(app.reload_queue.begin());
    if (app.reload_queue.empty()) {
        app.reload_due = {};
    }
    rv_editor_app_reload_send(app, file);
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

void rv_editor_app_build_restart_update(rv_editor_app &app, bool build_ended)
{
    if (build_ended) {
        rv_editor_app_build_restart_built(app);
    }
    rv_editor_app_build_restart_stopped(app);
}

} // namespace rv_editor
