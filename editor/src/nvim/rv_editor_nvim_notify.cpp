// rv_editor_nvim::notified implementation: handle nvim notifications.

#include "nvim/rv_editor_nvim.hpp"

namespace rv_editor
{

namespace
{

using mtype = rv_editor_mpack::rv_editor_mpack_type;

} // namespace

void rv_editor_nvim::notified(const std::string &method, const rv_editor_mpack &params, rv_editor_log &log)
{
    if (method == "redraw") {
        screen_.apply(params);
        return;
    }
    if (method == "rv_mode" && !params.items.empty()) {
        vim_mode_ = params.items[0].b;
        return;
    }
    if (method == "rv_lsp" && !params.items.empty()) {
        const rv_editor_mpack &m = params.items[0];
        const rv_editor_mpack *server = m.get("server");
        const rv_editor_mpack *state = m.get("state");
        if (server == nullptr || state == nullptr) {
            return;
        }
        const rv_editor_mpack *reason = m.get("reason");
        rv_editor_nvim_lsp &entry = lsp_[server->s];
        const std::string new_reason = reason != nullptr ? reason->s : std::string();
        if (entry.state == state->s && entry.reason == new_reason) {
            return;
        }
        entry.state = state->s;
        entry.reason = new_reason;
        log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
            server->s + ": " + entry.state + (entry.reason.empty() ? "" : (": " + entry.reason)));
        return;
    }
    if (method == "rv_diagnostics" && !params.items.empty()) {
        const rv_editor_mpack &m = params.items[0];
        const rv_editor_mpack *file = m.get("file");
        const rv_editor_mpack *items = m.get("items");
        if (file == nullptr || !file->is(mtype::string) || items == nullptr || !items->is(mtype::array)) {
            return;
        }
        std::vector<rv_editor_nvim_diagnostic> out;
        for (const rv_editor_mpack &it : items->items) {
            const rv_editor_mpack *line = it.get("line");
            const rv_editor_mpack *col = it.get("col");
            const rv_editor_mpack *severity = it.get("severity");
            const rv_editor_mpack *message = it.get("message");
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
            const rv_editor_mpack *source = it.get("source");
            d.source = source != nullptr && source->is(mtype::string) ? source->s : "lsp";
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
    if (method != "rv_buffers" || params.items.empty()) {
        return;
    }
    buffers_.clear();
    for (const rv_editor_mpack &b : params.items[0].items) {
        rv_editor_nvim_buffer buf;
        if (const rv_editor_mpack *v = b.get("id")) {
            buf.id = v->i;
        }
        if (const rv_editor_mpack *v = b.get("name")) {
            buf.name = v->s;
        }
        if (const rv_editor_mpack *v = b.get("modified")) {
            buf.modified = v->b;
        }
        if (const rv_editor_mpack *v = b.get("windows")) {
            for (const rv_editor_mpack &w : v->items) {
                buf.windows.push_back(w.i);
            }
        }
        buffers_.push_back(std::move(buf));
    }
}

} // namespace rv_editor
