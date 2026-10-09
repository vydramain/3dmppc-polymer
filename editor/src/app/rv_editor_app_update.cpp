// The editor's model, advanced once per frame.

#include "app/rv_editor_app.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace rv_editor
{

namespace
{

// Requests queued by the panes; taken out first, so one queued while running waits.
// Every request is tried; the first failure is returned (each refusal is logged by its request).
int rv_editor_app_update_requests(rv_editor_app &app)
{
    const std::vector<rv_editor_pane_request> requests = std::exchange(app.requests, {});
    int result = RV_OK;
    for (const rv_editor_pane_request &request : requests) {
        const int request_result = rv_editor_app_request_run(app, request);
        result = result == RV_OK ? request_result : result;
    }
    if (result != RV_OK) {
        return result;
    }
    return RV_OK;
}

// Every shell reads on, behind another tab too, so none stalls on a full terminal.
// Every shell is read; each failure is logged here, the first is returned.
int rv_editor_app_update_terminals(rv_editor_app &app)
{
    int result = RV_OK;
    for (auto &[pane, view] : app.terminals) {
        if (view.term == nullptr) {
            continue;
        }
        std::string error;
        const int update_result = view.term->update(error);
        if (update_result != RV_OK) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "terminal: " + error);
        }
        result = result == RV_OK ? update_result : result;
    }
    if (result != RV_OK) {
        return result;
    }
    return RV_OK;
}

// Shells of closed tiles go on ending, one step a frame; the ended ones leave the list.
// Every shell takes its step; each failure is logged here, the first is returned.
int rv_editor_app_update_stopping(rv_editor_app &app)
{
    int result = RV_OK;
    for (std::unique_ptr<rv_editor_pty> &pty : app.stopping_ptys) {
        std::string error;
        const int stop_result = pty->stop_step(error);
        if (stop_result != RV_OK) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "terminal: " + error);
        }
        result = result == RV_OK ? stop_result : result;
    }
    std::erase_if(app.stopping_ptys, [](const std::unique_ptr<rv_editor_pty> &pty) {
        return !pty->stopping();
    });
    if (result != RV_OK) {
        return result;
    }
    return RV_OK;
}

// Failure texts: the session's and the build's file name what the user lost; any other file gets the generic one.
constexpr std::string_view session_failed_text = "session log not kept: ";
constexpr std::string_view build_failed_text = "build log not kept: ";
constexpr std::string_view file_failed_text = "log file not written: ";

// Queues this frame's new log lines to the open files, before any clear of the log.
// Every failed write is logged with its prefix; the first failure is returned.
int rv_editor_app_update_log_files(rv_editor_app &app)
{
    app.log_files.feed(app.log);
    std::string error;
    int64_t pid = 0;
    int result = RV_OK;
    int poll_result = app.log_files.poll(error, pid);
    while (poll_result != RV_OK) {
        const bool is_session = pid == app.session.pid();
        const bool is_build = pid == app.build.pid();
        const std::string_view prefix = is_session ? session_failed_text : is_build ? build_failed_text : file_failed_text;
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, std::string(prefix) + error);
        result = result == RV_OK ? poll_result : result;
        error.clear();
        poll_result = app.log_files.poll(error, pid);
    }
    if (result != RV_OK) {
        return result;
    }
    return RV_OK;
}

// The watcher's suffix when no watch runs: Refresh is then the only way to see changes.
constexpr std::string_view files_unwatched_text = "; files are not watched, Refresh shows changes";

// Watches the files; a failure is logged with the watcher's reason, and the suffix when nothing is watched.
int rv_editor_app_update_files(rv_editor_app &app)
{
    std::string error;
    const int result = app.files.update(app.log, error);
    const bool failed = result != RV_OK;
    const bool unwatched = failed && !app.files.watching();
    const std::string_view suffix = unwatched ? files_unwatched_text : std::string_view();
    if (failed) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "file watch: " + error + std::string(suffix));
        return result;
    }
    return RV_OK;
}

// The build's update; a failure is logged as the burner's.
int rv_editor_app_update_build(rv_editor_app &app)
{
    std::string error;
    const int result = app.build.update(app.log, error);
    if (result != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "burner: " + error);
        return result;
    }
    return RV_OK;
}

// The session's update; a failure is logged as the runtime's.
int rv_editor_app_update_session(rv_editor_app &app)
{
    std::string error;
    const int result = app.session.update(app.log, error);
    if (result != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "runtime: " + error);
        return result;
    }
    return RV_OK;
}

// A tool's `--version` check runs behind the frame; one poll takes one step. A failure is logged and
// returned.
int rv_editor_app_update_tool_version(rv_editor_app &app, rv_editor_tool &tool, const std::string &name)
{
    std::string error;
    const int poll_result = rv_editor_tool_version_poll(tool, error);
    if (poll_result != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, name + ": " + error);
        return poll_result;
    }
    return RV_OK;
}

// Logs the tool's version once its check has ended; the frame asks that question.
int rv_editor_app_log_tool_version(rv_editor_app &app, const rv_editor_tool &tool, const std::string &name)
{
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, name + " version: " + tool.version);
    return RV_OK;
}

// Detects if a line is from the last build's output or a candidate being built.
bool rv_editor_app_line_from_latest_build(const rv_editor_log_line &line, uint64_t build_first_seq)
{
    return line.seq >= build_first_seq &&
        (line.source == rv_editor_log_source::build || line.source == rv_editor_log_source::candidate);
}

// The latest job's places. Cannot fail: called when the log has changed.
void rv_editor_app_collect_problems(rv_editor_app &app)
{
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

// Each candidate is polled; a dirty one is saved. Every candidate is tried; each failure is logged
// by the save; the first is returned.
int rv_editor_app_update_candidates(rv_editor_app &app)
{
    int result = RV_OK;
    for (rv_editor_candidate &c : app.release.candidates) {
        rv_editor_candidate_poll(c);
        if (c.dirty) {
            const int save_result = rv_editor_app_release_save_candidate(app, c);
            result = result == RV_OK ? save_result : result;
        }
    }
    if (result != RV_OK) {
        return result;
    }
    return RV_OK;
}

} // namespace

// The editor frame: not an operation, so it returns nothing; it asks the questions below and calls the operations.
void rv_editor_app_update(rv_editor_app &app)
{
    // Each operation logs its own refusal, so the frame goes on and drops the codes it returns.
    (void)rv_editor_app_update_requests(app);
    (void)rv_editor_app_update_log_files(app);
    (void)rv_editor_app_update_files(app);
    app.nvim.update(app.log);
    (void)rv_editor_app_update_terminals(app);
    (void)rv_editor_app_update_stopping(app);

    // A `--version` check runs while it is running; the poll logs its own failure, the version once ended.
    const bool burner_checking = app.tools.burner.version_check.running();
    const int burner_poll_result =
        burner_checking ? rv_editor_app_update_tool_version(app, app.tools.burner, "burner") : RV_ERR_PENDING;
    const bool burner_ended = burner_checking && burner_poll_result == RV_OK && !app.tools.burner.version_check.running();
    if (burner_ended) {
        (void)rv_editor_app_log_tool_version(app, app.tools.burner, "burner");
    }
    const bool baker_checking = app.tools.baker.version_check.running();
    const int baker_poll_result =
        baker_checking ? rv_editor_app_update_tool_version(app, app.tools.baker, "baker") : RV_ERR_PENDING;
    const bool baker_ended = baker_checking && baker_poll_result == RV_OK && !app.tools.baker.version_check.running();
    if (baker_ended) {
        (void)rv_editor_app_log_tool_version(app, app.tools.baker, "baker");
    }

    // What changed on disk: nvim, candidates, reload queue and disc.toml follow it.
    const bool files_changed = !app.files.changed.empty();
    if (files_changed) {
        (void)rv_editor_app_changed_files_apply(app);
    }

    const bool was_busy = app.build.busy();
    (void)rv_editor_app_update_build(app);
    (void)rv_editor_app_update_session(app);

    // A build just ended: old builds go, the time is noted, a waiting Run runs.
    const bool build_ended = was_busy && !app.build.busy();
    if (build_ended) {
        rv_editor_app_build_ended_note(app);
    }
    // The build Run started runs, or Run says why not; never the one before.
    const bool run_for_build = build_ended && app.run_after_build;
    if (run_for_build) {
        app.run_after_build = false;
    }
    const bool run_built = run_for_build && rv_editor_app_build_restart_built(app);
    const bool run_refused = run_for_build && !run_built;
    if (run_refused) {
        (void)rv_editor_app_run_after_build_refuse(app);
    }
    if (run_built) {
        (void)rv_editor_app_run_after_build_start(app);
    }

    // The latest job's places, once per change of the log.
    const bool problems_stale = app.problems_revision != app.log.revision();
    if (problems_stale) {
        rv_editor_app_collect_problems(app);
    }

    // Release: the player's output transfer and finish, a finished candidate build, its revision, the records'
    // saves, the playtest's verdict and end.
    const bool player_exists = app.release.player != nullptr;
    if (player_exists) {
        (void)rv_editor_app_player_transfer(app);
    }
    const bool player_done = player_exists && rv_editor_app_player_output_complete(app);
    if (player_done) {
        (void)rv_editor_app_player_finish(app);
    }
    const bool release_build_ended = build_ended && app.release.building;
    if (release_build_ended) {
        (void)rv_editor_app_release_note_build_end(app);
    }
    // The lookup advances only while a candidate builds.
    const int revision = app.release.building ? rv_editor_app_release_revision_poll(app) : RV_ERR_PENDING;
    const bool revision_known = revision == RV_OK;
    const bool build_noted = revision_known && rv_editor_app_release_build_noted();
    if (build_noted) {
        (void)rv_editor_app_release_finish_build(app);
    }
    (void)rv_editor_app_update_candidates(app);
    const bool playing = rv_editor_app_release_playing(app);
    const bool still_playing = rv_editor_app_release_still_playing(app);
    const bool verdict_due = still_playing && app.session.connected() && rv_editor_app_release_loads_running(app);
    if (verdict_due) {
        (void)rv_editor_app_release_playtest_verdict(app);
    }
    const bool playtest_over = playing && !still_playing;
    if (playtest_over) {
        (void)rv_editor_app_release_playtest_end(app);
    }

    const bool run_after_stop_due = app.run_after_stop && !app.session.live();
    if (run_after_stop_due) {
        app.run_after_stop = false;
        (void)rv_editor_app_run(app);
    }

    // Build and Restart: a build it waited for has ended. A good one stops the live session, or starts
    // the new build when none runs; a failed one drops the restart. A stop that has ended starts it.
    const bool build_for_restart = build_ended && app.restart_after_build;
    if (build_for_restart) {
        app.restart_after_build = false;
    }
    const bool restart_built = build_for_restart && rv_editor_app_build_restart_built(app);
    const bool restart_refused = build_for_restart && !restart_built;
    const bool live = app.session.live();
    const bool restart_stop = restart_built && live;
    const bool restart_start = restart_built && !live;
    if (restart_refused) {
        (void)rv_editor_app_build_restart_refuse(app);
    }
    if (restart_stop) {
        (void)rv_editor_app_build_restart_stop(app);
    }
    if (restart_start) {
        (void)rv_editor_app_build_restart_start(app);
    }
    const bool restart_after_stop_due = app.restart_after_stop && !app.session.live();
    if (restart_after_stop_due) {
        app.restart_after_stop = false;
        (void)rv_editor_app_build_restart_start(app);
    }

    // The running bake: its output joins the log each frame; once the baker has ended the bake
    // is judged, and a good one goes to the session. Drained even if the session has ended.
    const bool bake_running = rv_editor_app_texture_bake_busy(app);
    if (bake_running) {
        (void)rv_editor_app_texture_bake_transfer(app);
        const bool bake_exited = rv_editor_app_texture_bake_reap(app) == RV_OK;
        if (bake_exited) {
            (void)rv_editor_app_texture_bake_end(app);
            const bool bake_good = rv_editor_app_texture_bake_ok(app);
            if (bake_good) {
                (void)rv_editor_app_texture_bake_send(app);
            }
        }
    }

    // Reload On Save: the queue's oldest file goes out once it has settled and the session can take it.
    // A queue without a live session is dropped.
    const bool session_live = app.session.live();
    if (!session_live) {
        rv_editor_app_reload_queue_drop(app);
    }
    const bool queue_pending = session_live && !app.reload_queue.empty();
    const bool queue_due = queue_pending && rv_editor_app_reload_due(app);
    const bool queue_sendable = queue_due && rv_editor_app_can_reload_now(app);
    if (queue_sendable) {
        (void)rv_editor_app_reload_queue_send(app);
    }
}

} // namespace rv_editor
