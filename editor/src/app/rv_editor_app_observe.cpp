// Observe: the app's reads of the running session's Lua state (keys, pinned values, status).
// The pane shows what they leave in the session's answers and in app.observe.

#include "app/rv_editor_app.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// The requests that read the session: a pane waits for them to be answered.
bool rv_editor_observe_request_kind(const rv_editor_pane_request &request)
{
    switch (request.kind) {
    case rv_editor_pane_request_kind::observe_read:
    case rv_editor_pane_request_kind::observe_open:
    case rv_editor_pane_request_kind::observe_pin:
    case rv_editor_pane_request_kind::observe_unpin:
    case rv_editor_pane_request_kind::observe_refresh:
        return true;
    default:
        return false;
    }
}

// Writes a refused query or refresh: a busy runtime is a warning, any other refusal an error.
void rv_editor_observe_refusal(rv_editor_app &app, int rv, const std::string &prefix, const std::string &error)
{
    const rv_editor_log_level level = rv == RV_ERR_BUSY ? rv_editor_log_level::warning : rv_editor_log_level::error;
    app.log.add(rv_editor_log_source::editor,
        level,
        prefix + error,
        rv_editor_log_channel::none,
        app.session.pid(),
        app.session.number());
}

} // namespace

std::string rv_editor_observe_request(const char *verb, const std::vector<std::string> &path)
{
    std::string out = verb;
    for (const std::string &key : path) {
        out += " " + key;
    }
    return out;
}

int rv_editor_app_observe_query(rv_editor_app &app, const std::string &request)
{
    std::string error;
    const int rv = app.session.query(request, app.log, error);
    if (rv != RV_OK) {
        rv_editor_observe_refusal(app, rv, "cannot query '" + request + "': ", error);
        return rv;
    }
    return RV_OK;
}

int rv_editor_app_observe_read(rv_editor_app &app)
{
    rv_editor_observe &o = app.observe;
    // Stops at the first refusal: the rest would be refused the same way.
    int rv = rv_editor_app_observe_query(app, rv_editor_observe_request(observe_verb_keys, o.path));
    for (auto p = o.pins.begin(); rv == RV_OK && p != o.pins.end(); ++p) {
        rv = rv_editor_app_observe_query(app, std::string(observe_verb_get) + *p);
    }
    // Also after a refusal: a paused pane reads only when read_frame differs, so no retry every frame.
    o.read_frame = app.session.frame();
    return rv;
}

int rv_editor_app_observe_refresh(rv_editor_app &app)
{
    std::string error;
    const int rv = app.session.refresh(app.log, error);
    if (rv != RV_OK) {
        rv_editor_observe_refusal(app, rv, "cannot refresh the runtime status: ", error);
        // The paused read would ask again this frame and be refused the same way.
        app.observe.read_frame = app.session.frame();
        return rv;
    }
    return rv_editor_app_observe_read(app);
}

int rv_editor_app_observe_open(rv_editor_app &app, const std::string &path)
{
    // Split at the console's separator; an empty piece (a doubled space) is no key.
    std::istringstream keys(path);
    app.observe.path.clear();
    std::string key;
    while (std::getline(keys, key, ' ')) {
        if (!key.empty()) {
            app.observe.path.push_back(key);
        }
    }
    return rv_editor_app_observe_read(app);
}

int rv_editor_app_observe_pin(rv_editor_app &app, const std::string &key)
{
    rv_editor_observe &o = app.observe;
    if (std::ranges::find(o.pins, key) != o.pins.end()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "already pinned: " + key);
        return RV_ERR_ALREADY;
    }
    o.pins.push_back(key);
    return rv_editor_app_observe_query(app, std::string(observe_verb_get) + key);
}

int rv_editor_app_observe_unpin(rv_editor_app &app, const std::string &key)
{
    rv_editor_observe &o = app.observe;
    const auto pin = std::ranges::find(o.pins, key);
    if (pin == o.pins.end()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "not pinned: " + key);
        return RV_ERR_ALREADY;
    }
    o.pins.erase(pin);
    return RV_OK;
}

bool rv_editor_app_observe_waiting(const rv_editor_app &app)
{
    const bool queued = std::ranges::any_of(app.requests, rv_editor_observe_request_kind);
    return queued || app.session.query_waiting();
}

} // namespace rv_editor
