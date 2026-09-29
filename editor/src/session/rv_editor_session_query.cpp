// The session's reads and reloads: status facts, `get`/`keys` answers, `reload entry`/`reload module`.

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

// The module name out of a `reload module <name>` verb; empty for `reload entry`.
std::string rv_editor_reload_module(const std::string &verb)
{
    constexpr std::string_view prefix = "reload module ";
    if (verb.starts_with(prefix)) {
        return verb.substr(prefix.size());
    }
    return {};
}

} // namespace

int64_t rv_editor_session::query(const std::string &request, rv_editor_log &log)
{
    if (!handshake_done_ || !proc_.running() || quit_sent_) {
        return 0;
    }
    return send(request, log);
}

void rv_editor_session::reload(rv_editor_log &log, const std::string &module)
{
    if (!handshake_done_ || !proc_.running() || quit_sent_ || reloading_) {
        return;
    }
    const std::string verb = module.empty() ? "reload entry" : "reload module " + module;
    const int64_t id = send(verb, log);
    if (id != 0) {
        reloading_ = true;
        reload_id_ = id;
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
    if (req.verb != "reload entry" && !req.verb.starts_with("reload module ")) {
        return false;
    }
    // A late answer for a superseded reload (msg.id != reload_id_) is logged under
    // its own target, but the flag and the shown result belong to the current one.
    const std::string target = rv_editor_reload_module(req.verb);
    const bool current = msg.id == reload_id_;
    if (current) {
        reloading_ = false;
        reload_target_ = target;
    }
    if (err) {
        // The previous code stays; whether its effects ran is the runtime's word (RLD-04).
        const std::string result = std::string(msg.get("error")) + ": " + rv_editor_hex_decode(msg.get("msg")) +
            (msg.get("effects") == "1" ? " (effects may have happened before it failed)" : "");
        const std::string named = target.empty() ? std::string() : "module " + target + ": ";
        log.add(rv_editor_log_source::runtime, rv_editor_log_level::error, "reload refused: " + named + result);
        if (current) {
            reload_ok_ = false;
            reload_result_ = result;
        }
        if (proc_.running()) {
            send("status", log);
        }
        return true;
    }
    std::string result;
    if (target.empty()) {
        facts_.revision = rv_editor_field_int(msg, "entry_revision", facts_.revision);
        facts_.entry_hash = msg.get("entry_hash");
        facts_.lua_used = rv_editor_field_int(msg, "lua_used", facts_.lua_used);
        facts_.at = std::chrono::system_clock::now();
        result = "entry revision " + std::to_string(facts_.revision);
    } else {
        result = "module " + target + ", hash " + std::string(msg.get("hash"));
    }
    log.add(rv_editor_log_source::runtime, rv_editor_log_level::info, "reload accepted: " + result);
    if (current) {
        reload_ok_ = true;
        reload_result_ = result;
    }
    return true;
}

void rv_editor_session::note_reload_timeout(int64_t id, const std::string &verb)
{
    if (id != reload_id_ || (verb != "reload entry" && !verb.starts_with("reload module "))) {
        return;
    }
    reloading_ = false;
    reload_target_ = rv_editor_reload_module(verb);
    reload_ok_ = false;
    reload_result_ = "no answer: whether it applied is unknown";
}

} // namespace rv_editor
