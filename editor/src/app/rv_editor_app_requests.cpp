// Requests from the panes (rv_editor_pane_request): queued, then run by the app's frame.

#include "app/rv_editor_app.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

using rv_editor_request_op = int (*)(rv_editor_app &app, const rv_editor_pane_request &request);

// Kinds that need no data from the request: the operation ignores it.
int rv_editor_request_stop(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_stop(app);
}

int rv_editor_request_force_stop(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_force_stop(app);
}

int rv_editor_request_restart(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_restart(app);
}

int rv_editor_request_cancel_build(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_build_cancel(app);
}

int rv_editor_request_refresh_files(rv_editor_app &app, const rv_editor_pane_request &)
{
    app.files.refresh();
    return RV_OK;
}

int rv_editor_request_capture_frame(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_capture_frame(app);
}

int rv_editor_request_save_finding(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_save_finding(app);
}

int rv_editor_request_case_new(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_case_new(app);
}

// The case result: the case file named by the request (empty: exploratory), the choice is the result.
int rv_editor_request_case_result(rv_editor_app &app, const rv_editor_pane_request &request)
{
    const bool chosen = request.choice < static_cast<size_t>(rv_editor_case_result_kind::count);
    if (!chosen) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "test case result refused: no result chosen");
        return RV_ERR_INVAL;
    }
    const bool exploratory = request.path.empty();
    rv_editor_case c = rv_editor_case_exploratory();
    std::string error;
    const int read_rv = exploratory ? RV_OK : rv_editor_case_read(request.path, c, error);
    if (read_rv != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "test case result refused: " + error);
        return read_rv;
    }
    return rv_editor_app_case_result(app, c, static_cast<rv_editor_case_result_kind>(request.choice));
}

int rv_editor_request_cases_list(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_cases_list(app);
}

int rv_editor_request_add_mark(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_app_add_mark(app, request.name);
}

int rv_editor_request_report_problem(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_report_problem(app);
}

int rv_editor_request_dismiss_findings_error(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_findings_dismiss_error(app);
}

int rv_editor_request_scene_move(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_app_scene_move(app, request.name, request.parent, request.keep_world);
}

// Dismiss clears scene_move_note; the Hierarchy pane only draws it.
int rv_editor_request_dismiss_scene_note(rv_editor_app &app, const rv_editor_pane_request &)
{
    app.scene_move_note.clear();
    return RV_OK;
}

int rv_editor_request_stop_player(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_stop_player(app);
}

int rv_editor_request_observe_read(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_observe_read(app);
}

int rv_editor_request_observe_open(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_app_observe_open(app, request.name);
}

int rv_editor_request_observe_pin(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_app_observe_pin(app, request.name);
}

int rv_editor_request_observe_unpin(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_app_observe_unpin(app, request.name);
}

int rv_editor_request_observe_refresh(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_observe_refresh(app);
}

int rv_editor_request_profile_select(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_app_profile_select(app, request.choice);
}

int rv_editor_request_profile_add(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_profile_add(app);
}

int rv_editor_request_profile_delete(rv_editor_app &app, const rv_editor_pane_request &)
{
    return rv_editor_app_profile_delete(app);
}

int rv_editor_request_profile_apply(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_app_profile_apply(app, request.profile);
}

int rv_editor_request_profile_apply_restart(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_app_profile_apply_restart(app, request.profile);
}

// A dialog's operation: runs on the request's entry or directory; the reason goes to error when it fails.
using rv_editor_dialog_op = int (*)(rv_editor_app &app, const rv_editor_pane_request &request, std::string &error);

// The new file is selected and opened next: the user asked for it and edits it next.
int rv_editor_dialog_create_file(rv_editor_app &app, const rv_editor_pane_request &request, std::string &error)
{
    const int err = rv_editor_app_create_file(app, request.path, request.name, error);
    if (err == RV_OK) {
        const std::filesystem::path file = request.path / request.name;
        app.files_select = file;
        app.open_requests.push_back({ file, 0 });
    }
    return err;
}

int rv_editor_dialog_create_dir(rv_editor_app &app, const rv_editor_pane_request &request, std::string &error)
{
    return rv_editor_app_create_dir(app, request.path, request.name, error);
}

int rv_editor_dialog_rename(rv_editor_app &app, const rv_editor_pane_request &request, std::string &error)
{
    const int err = rv_editor_app_rename(app, request.path, request.name, error);
    if (err == RV_OK) {
        app.files_select = request.path.parent_path() / request.name;
    }
    return err;
}

int rv_editor_dialog_remove(rv_editor_app &app, const rv_editor_pane_request &request, std::string &error)
{
    const int err = rv_editor_app_remove(app, request.path, error);
    if (err == RV_OK) {
        app.files_select = std::filesystem::path();
    }
    return err;
}

// Runs a Files dialog's operation, then writes its outcome to the asking pane's view: a failure
// keeps the dialog open with the reason, a success closes it. No view, nothing runs.
int rv_editor_request_dialog(rv_editor_app &app, const rv_editor_pane_request &request, rv_editor_dialog_op run)
{
    if (!app.files_views.contains(request.pane)) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "files request: its pane has no view");
        return RV_ERR_INVAL;
    }
    std::string error;
    const int err = run(app, request, error);
    // Looked up after the run: no view reference is held across the operation.
    rv_editor_files_view &view = app.files_views[request.pane];
    if (err != RV_OK) {
        view.error = error;
        return err;
    }
    view.dialog = rv_editor_files_view::rv_editor_files_dialog::none;
    view.error.clear();
    return RV_OK;
}

int rv_editor_request_create_file(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_request_dialog(app, request, rv_editor_dialog_create_file);
}

int rv_editor_request_create_dir(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_request_dialog(app, request, rv_editor_dialog_create_dir);
}

int rv_editor_request_rename(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_request_dialog(app, request, rv_editor_dialog_rename);
}

int rv_editor_request_remove(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_request_dialog(app, request, rv_editor_dialog_remove);
}

int rv_editor_request_texture_bake(rv_editor_app &app, const rv_editor_pane_request &request)
{
    return rv_editor_app_texture_bake_start(app, request.name, request.path);
}

struct rv_editor_request_entry {
    rv_editor_pane_request_kind kind;
    rv_editor_request_op run;
    std::string_view name;
};

// Each kind's operation and name for the log; a new kind adds one row.
constexpr rv_editor_request_entry rv_editor_request_table[] = {
    { rv_editor_pane_request_kind::stop, rv_editor_request_stop, "stop" },
    { rv_editor_pane_request_kind::force_stop, rv_editor_request_force_stop, "force stop" },
    { rv_editor_pane_request_kind::restart, rv_editor_request_restart, "restart" },
    { rv_editor_pane_request_kind::cancel_build, rv_editor_request_cancel_build, "cancel build" },
    { rv_editor_pane_request_kind::create_file, rv_editor_request_create_file, "create file" },
    { rv_editor_pane_request_kind::create_dir, rv_editor_request_create_dir, "create dir" },
    { rv_editor_pane_request_kind::rename, rv_editor_request_rename, "rename" },
    { rv_editor_pane_request_kind::remove, rv_editor_request_remove, "remove" },
    { rv_editor_pane_request_kind::refresh_files, rv_editor_request_refresh_files, "refresh files" },
    { rv_editor_pane_request_kind::capture_frame, rv_editor_request_capture_frame, "capture frame" },
    { rv_editor_pane_request_kind::save_finding, rv_editor_request_save_finding, "save finding" },
    { rv_editor_pane_request_kind::case_new, rv_editor_request_case_new, "case new" },
    { rv_editor_pane_request_kind::case_result, rv_editor_request_case_result, "case result" },
    { rv_editor_pane_request_kind::add_mark, rv_editor_request_add_mark, "add mark" },
    { rv_editor_pane_request_kind::report_problem, rv_editor_request_report_problem, "report problem" },
    { rv_editor_pane_request_kind::dismiss_findings_error, rv_editor_request_dismiss_findings_error, "dismiss findings error" },
    { rv_editor_pane_request_kind::observe_read, rv_editor_request_observe_read, "observe read" },
    { rv_editor_pane_request_kind::observe_open, rv_editor_request_observe_open, "observe open" },
    { rv_editor_pane_request_kind::observe_pin, rv_editor_request_observe_pin, "observe pin" },
    { rv_editor_pane_request_kind::observe_unpin, rv_editor_request_observe_unpin, "observe unpin" },
    { rv_editor_pane_request_kind::observe_refresh, rv_editor_request_observe_refresh, "observe refresh" },
    { rv_editor_pane_request_kind::profile_select, rv_editor_request_profile_select, "profile select" },
    { rv_editor_pane_request_kind::profile_add, rv_editor_request_profile_add, "profile add" },
    { rv_editor_pane_request_kind::profile_delete, rv_editor_request_profile_delete, "profile delete" },
    { rv_editor_pane_request_kind::profile_apply, rv_editor_request_profile_apply, "profile apply" },
    { rv_editor_pane_request_kind::profile_apply_restart, rv_editor_request_profile_apply_restart, "profile apply restart" },
    { rv_editor_pane_request_kind::texture_bake, rv_editor_request_texture_bake, "texture bake" },
    { rv_editor_pane_request_kind::scene_move, rv_editor_request_scene_move, "scene move" },
    { rv_editor_pane_request_kind::dismiss_scene_note, rv_editor_request_dismiss_scene_note, "dismiss scene note" },
    { rv_editor_pane_request_kind::stop_player, rv_editor_request_stop_player, "stop player" },
    { rv_editor_pane_request_kind::cases_list, rv_editor_request_cases_list, "cases list" },
};

// Named in the log when a kind has no table row.
constexpr std::string_view rv_editor_request_unknown_name = "unknown";

std::string_view rv_editor_request_name(rv_editor_pane_request_kind kind)
{
    const auto entry = std::ranges::find(rv_editor_request_table, kind, &rv_editor_request_entry::kind);
    return entry == std::end(rv_editor_request_table) ? rv_editor_request_unknown_name : entry->name;
}

} // namespace

int rv_editor_app_restart(rv_editor_app &app)
{
    if (!app.session.live()) {
        const std::string text = "restart refused: no session runs";
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, text);
        return RV_ERR_INVAL;
    }
    app.run_after_stop = true;
    return rv_editor_app_stop(app);
}

int rv_editor_app_request(rv_editor_app &app, const rv_editor_pane_request &request)
{
    const bool waiting = std::ranges::any_of(app.requests, [&request](const rv_editor_pane_request &queued) {
        return queued.kind == request.kind;
    });
    if (waiting) {
        const std::string text = "request already waiting: " + std::string(rv_editor_request_name(request.kind));
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, text);
        return RV_ERR_ALREADY;
    }
    app.requests.push_back(request);
    return RV_OK;
}

bool rv_editor_app_request_pending(const rv_editor_app &app, rv_editor_pane_request_kind kind)
{
    const bool queued = std::ranges::any_of(app.requests, [kind](const rv_editor_pane_request &request) {
        return request.kind == kind;
    });
    // Restart also covers the run after its stop, that run's unsaved-files question (asked or open),
    // and a waiting apply-and-restart, which restarts once it has applied.
    const bool run_asked = app.unsaved_ask == rv_editor_unsaved_ask::run || app.unsaved_open == rv_editor_unsaved_ask::run;
    const bool apply_restart_waiting = std::ranges::any_of(app.requests, [](const rv_editor_pane_request &request) {
        return request.kind == rv_editor_pane_request_kind::profile_apply_restart;
    });
    const bool restarting =
        kind == rv_editor_pane_request_kind::restart && (app.run_after_stop || run_asked || apply_restart_waiting);
    return queued || restarting;
}

int rv_editor_app_request_run(rv_editor_app &app, const rv_editor_pane_request &request)
{
    const auto entry = std::ranges::find(rv_editor_request_table, request.kind, &rv_editor_request_entry::kind);
    if (entry == std::end(rv_editor_request_table)) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "unknown request kind");
        return RV_ERR_INVAL;
    }
    return entry->run(app, request);
}

} // namespace rv_editor
