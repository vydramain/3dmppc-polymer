// The session's reads and reloads: status facts, `get`/`keys` answers, `reload entry`.

#include "session/rv_editor_session.hpp"

#include <charconv>

namespace rv_editor
{

namespace
{

int64_t rv_editor_field_int(const rv_editor_devmsg &msg, std::string_view key, int64_t fallback)
{
    const std::string_view s = msg.get(key);
    int64_t v = fallback;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

} // namespace

int64_t rv_editor_session::query(const std::string &request, rv_editor_log &log)
{
    if (!handshake_done_ || !proc_.running() || quit_sent_) {
        return 0;
    }
    return send(request, log);
}

void rv_editor_session::reload(rv_editor_log &log)
{
    if (!handshake_done_ || !proc_.running() || quit_sent_ || reloading_) {
        return;
    }
    if (send("reload entry", log) != 0) {
        reloading_ = true;
    }
}

void rv_editor_session::refresh(rv_editor_log &log)
{
    if (handshake_done_ && proc_.running() && !quit_sent_) {
        send("status", log);
    }
}

void rv_editor_session::note_facts(const rv_editor_devmsg &msg)
{
    facts_.disc = rv_editor_hex_decode(msg.get("disc"));
    facts_.code_hash = msg.get("disc_hash");
    facts_.pdk = msg.get("pdk");
    facts_.medium = msg.get("medium");
    facts_.reloadable = msg.get("entry_reloadable") == "1";
    facts_.revision = rv_editor_field_int(msg, "entry_revision", facts_.revision);
    if (facts_.first_revision < 0) {
        facts_.first_revision = facts_.revision;
    }
    facts_.entry_hash = msg.get("entry_hash");
    facts_.lua_used = rv_editor_field_int(msg, "lua_used", facts_.lua_used);
    facts_.lua_budget = rv_editor_field_int(msg, "lua_budget", facts_.lua_budget);
    facts_.at = std::chrono::system_clock::now();
}

bool rv_editor_session::handle_query(const rv_editor_request &req, const rv_editor_devmsg &msg, rv_editor_log &log)
{
    const bool err = msg.kind == rv_editor_devmsg::rv_editor_devmsg_kind::err;
    if (req.verb.starts_with("get ") || req.verb.starts_with("keys ") || req.verb == "keys") {
        // An answer describes the frame it was asked on only when the machine was
        // paused then: nothing ran between the question and the reading.
        rv_editor_answer &a = answers_[req.verb];
        a.ok = !err;
        a.fields = msg.fields;
        a.error = err ? std::string(msg.get("error")) + ": " + rv_editor_hex_decode(msg.get("msg")) : std::string();
        a.frame_exact = req.paused;
        a.frame = req.frame;
        a.at = std::chrono::system_clock::now();
        return true;
    }
    if (req.verb != "reload entry") {
        return false;
    }
    reloading_ = false;
    if (err) {
        // The previous code stays; whether its effects ran is the runtime's word (RLD-04).
        reload_ok_ = false;
        reload_result_ = std::string(msg.get("error")) + ": " + rv_editor_hex_decode(msg.get("msg")) +
            (msg.get("effects") == "1" ? " (effects may have happened before it failed)" : "");
        log.add(rv_editor_log_source::runtime, rv_editor_log_level::error, "reload refused: " + reload_result_);
        if (proc_.running()) {
            send("status", log);
        }
        return true;
    }
    facts_.revision = rv_editor_field_int(msg, "entry_revision", facts_.revision);
    facts_.entry_hash = msg.get("entry_hash");
    facts_.lua_used = rv_editor_field_int(msg, "lua_used", facts_.lua_used);
    facts_.at = std::chrono::system_clock::now();
    reload_ok_ = true;
    reload_result_ = "entry revision " + std::to_string(facts_.revision);
    log.add(rv_editor_log_source::runtime, rv_editor_log_level::info, "reload accepted: " + reload_result_);
    return true;
}

} // namespace rv_editor
