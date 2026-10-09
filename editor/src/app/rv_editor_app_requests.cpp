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

using rv_editor_request_op = int (*)(rv_editor_app &app);

struct rv_editor_request_entry {
    rv_editor_pane_request_kind kind;
    rv_editor_request_op run;
    std::string_view name;
};

// Each kind's operation and name for the log; a new kind adds one row.
constexpr rv_editor_request_entry rv_editor_request_table[] = {
    { rv_editor_pane_request_kind::stop, rv_editor_app_stop, "stop" },
    { rv_editor_pane_request_kind::force_stop, rv_editor_app_force_stop, "force stop" },
    { rv_editor_pane_request_kind::restart, rv_editor_app_restart, "restart" },
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
    return entry->run(app);
}

} // namespace rv_editor
