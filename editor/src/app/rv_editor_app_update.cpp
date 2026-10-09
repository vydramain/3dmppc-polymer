// The editor's model, advanced once per frame.

#include "app/rv_editor_app.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace rv_editor
{

namespace
{

// Every shell reads on, behind another tab too, so none stalls on a full terminal.
void rv_editor_app_update_terminals(rv_editor_app &app)
{
    for (auto &[pane, view] : app.terminals) {
        if (view.term == nullptr) {
            continue;
        }
        std::string error;
        if (view.term->update(error) != RV_OK) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "terminal: " + error);
        }
    }
}

// Shells of closed tiles go on ending, one step a frame; the ended ones leave the list.
void rv_editor_app_update_stopping(rv_editor_app &app)
{
    for (std::unique_ptr<rv_editor_pty> &pty : app.stopping_ptys) {
        std::string error;
        if (pty->stop_step(error) != RV_OK) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "terminal: " + error);
        }
    }
    std::erase_if(app.stopping_ptys, [](const std::unique_ptr<rv_editor_pty> &pty) {
        return !pty->stopping();
    });
}

// What changed on disk: nvim, candidates, reload queue and disc.toml follow it.
void rv_editor_app_update_changed_files(rv_editor_app &app)
{
    if (app.files.changed.empty()) {
        return;
    }
    // Clean buffers follow the disk; nvim asks about modified ones.
    app.nvim.checktime();
    rv_editor_app_release_changed(app);
    app.inputs_changed = true;
    const bool reload_on_save = app.run_config.profiles[app.run_config.active].reload_on_save;
    for (const std::filesystem::path &changed : app.files.changed) {
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
}

// A build just ended: old builds go, the time is noted, a waiting Run runs.
void rv_editor_app_update_build_ended(rv_editor_app &app, bool build_ended)
{
    if (!build_ended) {
        return;
    }
    app.build.prune(app.session.live() ? app.session.disc_dir() : std::filesystem::path());
    app.build_ended = std::filesystem::file_time_type::clock::now();
    // The build Run started runs, or Run says why not; never the one before.
    if (!app.run_after_build) {
        return;
    }
    app.run_after_build = false;
    const bool built = app.build.dev_state() == rv_editor_build_state::succeeded && app.build.last_success();
    if (!built) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            std::string("not run: the build ") + rv_editor_build_state_name(app.build.dev_state()));
        return;
    }
    (void)rv_editor_app_run_session(app, *app.build.last_success());
}

// The running bake: its output joins the log each frame; once the baker has ended the bake
// is judged, and a good one goes to the session. Drained even if the session has ended.
void rv_editor_app_update_texture_bake(rv_editor_app &app)
{
    if (!rv_editor_app_texture_bake_busy(app)) {
        return;
    }
    (void)rv_editor_app_texture_bake_transfer(app);
    if (rv_editor_app_texture_bake_reap(app) != RV_OK) {
        return;
    }
    (void)rv_editor_app_texture_bake_end(app);
    if (!rv_editor_app_texture_bake_ok(app)) {
        return;
    }
    (void)rv_editor_app_texture_bake_send(app);
}

// Reload On Save: the queue's oldest file goes out once it has settled and the session can take it.
void rv_editor_app_update_reload_queue(rv_editor_app &app)
{
    if (!app.session.live()) {
        rv_editor_app_reload_queue_drop(app);
        return;
    }
    if (app.reload_queue.empty()) {
        return;
    }
    if (!rv_editor_app_reload_due(app)) {
        return;
    }
    if (!rv_editor_app_can_reload_now(app)) {
        return;
    }
    (void)rv_editor_app_reload_queue_send(app);
}

// Detects if a line is from the last build's output or a candidate being built.
bool rv_editor_app_line_from_latest_build(const rv_editor_log_line &line, uint64_t build_first_seq)
{
    return line.seq >= build_first_seq &&
        (line.source == rv_editor_log_source::build || line.source == rv_editor_log_source::candidate);
}

// The latest job's places, once per change of the log.
void rv_editor_app_update_problems(rv_editor_app &app)
{
    if (app.problems_revision == app.log.revision()) {
        return;
    }
    app.problems_revision = app.log.revision();
    app.problems.clear();
    for (const rv_editor_log_line &line : app.log.lines()) {
        if (!rv_editor_app_line_from_latest_build(line, app.build_first_seq)) {
            continue;
        }
        rv_editor_problem p;
        if (rv_editor_problem_parse(line.text, app.project.root, p)) {
            app.problems.push_back(std::move(p));
        }
    }
}

// A tool's `--version` check runs behind the frame; each frame polls it, and it logs the end once.
void rv_editor_app_update_tool_version(rv_editor_app &app, rv_editor_tool &tool, const std::string &name)
{
    if (!tool.version_check.running()) {
        return;
    }
    std::string error;
    if (rv_editor_tool_version_poll(tool, error) != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, name + ": " + error);
        return;
    }
    if (!tool.version_check.running()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, name + " version: " + tool.version);
    }
}

// Failure texts: the session's and the build's file name what the user lost; any other file gets the generic one.
constexpr std::string_view session_failed_text = "session log not kept: ";
constexpr std::string_view build_failed_text = "build log not kept: ";
constexpr std::string_view file_failed_text = "log file not written: ";

// Queues this frame's new log lines to the open files, before any clear of the log; reports each write failure.
void rv_editor_app_update_log_files(rv_editor_app &app)
{
    app.log_files.feed(app.log);
    std::string error;
    int64_t pid = 0;
    while (app.log_files.poll(error, pid) != RV_OK) {
        const bool is_session = pid == app.session.pid();
        const bool is_build = pid == app.build.pid();
        const std::string_view prefix = is_session ? session_failed_text : is_build ? build_failed_text : file_failed_text;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, std::string(prefix) + error);
        error.clear();
    }
}

} // namespace

void rv_editor_app_update(rv_editor_app &app)
{
    rv_editor_app_update_log_files(app);
    std::string files_error;
    if (app.files.update(app.log, files_error) != RV_OK) {
        if (!app.files.watching()) {
            files_error += "; files are not watched, Refresh shows changes";
        }
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "file watch: " + files_error);
    }
    app.nvim.update(app.log);
    rv_editor_app_update_terminals(app);
    rv_editor_app_update_stopping(app);
    rv_editor_app_update_tool_version(app, app.tools.burner, "burner");
    rv_editor_app_update_tool_version(app, app.tools.baker, "baker");
    rv_editor_app_update_changed_files(app);
    const bool was_busy = app.build.busy();
    std::string build_error;
    const int build_result = app.build.update(app.log, build_error);
    if (build_result != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "burner: " + build_error);
    }
    std::string session_error;
    const int session_result = app.session.update(app.log, session_error);
    if (session_result != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "runtime output not read: " + session_error);
    }
    const bool build_ended = was_busy && !app.build.busy();
    rv_editor_app_update_build_ended(app, build_ended);
    rv_editor_app_update_problems(app);
    rv_editor_app_release_update(app, build_ended);
    if (app.run_after_stop && !app.session.live()) {
        app.run_after_stop = false;
        rv_editor_app_run(app);
    }
    rv_editor_app_build_restart_update(app, build_ended);
    rv_editor_app_update_texture_bake(app);
    rv_editor_app_update_reload_queue(app);
}

} // namespace rv_editor
