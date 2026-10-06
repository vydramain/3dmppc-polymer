// rv_editor_nvim::notified implementation: handle nvim notifications.

#include "nvim/rv_editor_nvim.hpp"

namespace rv_editor
{

namespace
{

using mtype = rv_editor_mpack::rv_editor_mpack_type;

// Notification method names sent by editor/nvim/rv_editor_init.lua.
constexpr std::string_view method_redraw = "redraw";
constexpr std::string_view method_rv_mode = "rv_mode";
constexpr std::string_view method_rv_lsp = "rv_lsp";
constexpr std::string_view method_rv_diagnostics = "rv_diagnostics";
constexpr std::string_view method_rv_buffers = "rv_buffers";

// Payload field names for rv_lsp notification.
constexpr std::string_view field_server = "server";
constexpr std::string_view field_state = "state";
constexpr std::string_view field_reason = "reason";

// Payload field names for rv_diagnostics notification.
constexpr std::string_view field_file = "file";
constexpr std::string_view field_items = "items";
constexpr std::string_view field_line = "line";
constexpr std::string_view field_col = "col";
constexpr std::string_view field_severity = "severity";
constexpr std::string_view field_message = "message";
constexpr std::string_view field_source = "source";
constexpr std::string_view default_source_lsp = "lsp";

// Payload field names for rv_buffers notification.
constexpr std::string_view field_id = "id";
constexpr std::string_view field_name = "name";
constexpr std::string_view field_modified = "modified";
constexpr std::string_view field_windows = "windows";

} // namespace

void rv_editor_nvim::notified(const std::string &method, const rv_editor_mpack &params, rv_editor_log &log)
{
    if (method == method_redraw) {
        screen_.apply(params);
        return;
    }
    if (method == method_rv_mode && !params.items.empty()) {
        vim_mode_ = params.items[0].b;
        return;
    }
    if (method == method_rv_lsp && !params.items.empty()) {
        const rv_editor_mpack &m = params.items[0];
        const rv_editor_mpack *server = m.get(field_server);
        const rv_editor_mpack *state = m.get(field_state);
        if (server == nullptr || state == nullptr) {
            return;
        }
        const rv_editor_mpack *reason = m.get(field_reason);
        rv_editor_nvim_lsp &entry = lsp_[server->s];
        const std::string new_reason = reason != nullptr ? reason->s : std::string();
        if (entry.state == state->s && entry.reason == new_reason) {
            return;
        }
        entry.state = state->s;
        entry.reason = new_reason;
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::info,
            server->s + ": " + entry.state + (entry.reason.empty() ? "" : (": " + entry.reason)));
        return;
    }
    if (method == method_rv_diagnostics && !params.items.empty()) {
        const rv_editor_mpack &m = params.items[0];
        const rv_editor_mpack *file = m.get(field_file);
        const rv_editor_mpack *items = m.get(field_items);
        if (file == nullptr || !file->is(mtype::string) || items == nullptr || !items->is(mtype::array)) {
            return;
        }
        std::vector<rv_editor_nvim_diagnostic> out;
        for (const rv_editor_mpack &it : items->items) {
            const rv_editor_mpack *line = it.get(field_line);
            const rv_editor_mpack *col = it.get(field_col);
            const rv_editor_mpack *severity = it.get(field_severity);
            const rv_editor_mpack *message = it.get(field_message);
            const bool line_ok = line != nullptr && line->is(mtype::integer);
            const bool col_ok = col != nullptr && col->is(mtype::integer);
            const bool severity_ok = severity != nullptr && severity->is(mtype::string);
            const bool message_ok = message != nullptr && message->is(mtype::string);
            if (!line_ok || !col_ok || !severity_ok || !message_ok) {
                continue;
            }
            rv_editor_nvim_diagnostic d;
            d.line = static_cast<int32_t>(line->i);
            d.col = static_cast<int32_t>(col->i);
            d.severity = severity->s;
            d.message = message->s;
            const rv_editor_mpack *source = it.get(field_source);
            d.source = source != nullptr && source->is(mtype::string) ? source->s : default_source_lsp;
            out.push_back(std::move(d));
        }
        if (out.empty()) {
            diagnostics_.erase(file->s);
        } else {
            diagnostics_[file->s] = std::move(out);
        }
        return;
    }
    if (swap_notified(method, params, log)) {
        return;
    }
    if (method != method_rv_buffers || params.items.empty()) {
        return;
    }
    buffers_.clear();
    for (const rv_editor_mpack &b : params.items[0].items) {
        rv_editor_nvim_buffer buf;
        if (const rv_editor_mpack *v = b.get(field_id)) {
            buf.id = v->i;
        }
        if (const rv_editor_mpack *v = b.get(field_name)) {
            buf.name = v->s;
        }
        if (const rv_editor_mpack *v = b.get(field_modified)) {
            buf.modified = v->b;
        }
        if (const rv_editor_mpack *v = b.get(field_windows)) {
            for (const rv_editor_mpack &w : v->items) {
                buf.windows.push_back(w.i);
            }
        }
        buffers_.push_back(std::move(buf));
    }
}

} // namespace rv_editor
