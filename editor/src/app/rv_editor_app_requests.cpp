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

// A dialog's operation: runs on the request's entry or directory; the reason goes to error when it fails.
using rv_editor_dialog_op = int (*)(rv_editor_app &app, const rv_editor_pane_request &request, std::string &error);

// The new file is selected and opened next: the user asked for it and edits it next.
int rv_editor_dialog_create_file(rv_editor_app &app, const rv_editor_pane_request &request, std::string &error)
{
    const int err = rv_editor_app_create_file(app, request.path, request.name, error);
    if (err == RV_OK) {
        const std::filesystem::path file = request.path / request.name;
        app.files.selected = file;
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
    return rv_editor_app_rename(app, request.path, request.name, error);
}

int rv_editor_dialog_remove(rv_editor_app &app, const rv_editor_pane_request &request, std::string &error)
{
    return rv_editor_app_remove(app, request.path, error);
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
    // Restart also covers the run after its stop, and that run's unsaved-files question (asked or open).
    const bool run_asked = app.unsaved_ask == rv_editor_unsaved_ask::run || app.unsaved_open == rv_editor_unsaved_ask::run;
    const bool restarting = kind == rv_editor_pane_request_kind::restart && (app.run_after_stop || run_asked);
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
