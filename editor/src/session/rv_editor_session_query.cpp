// The session's reads and reloads: status facts, `get`/`keys` answers,
// `reload entry`/`reload module` and `asset <name> bytes <n>`.

#include "session/rv_editor_session.hpp"

#include <charconv>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Query request prefixes recognized by the protocol (src/rv_pconsole/rv_pconsole_cmd_devtools.cpp).
constexpr std::string_view cmd_get_prefix = "get ";
constexpr std::string_view cmd_keys_prefix = "keys ";
constexpr std::string_view cmd_keys_exact = "keys";

// Reload command verb for the entry chunk.
constexpr std::string_view reload_entry_verb = "reload entry";

// Reload command prefix for module reloading (name follows).
constexpr std::string_view reload_module_prefix = "reload module ";

// Asset command prefix with the asset name.
constexpr std::string_view asset_verb_prefix = "asset ";

// Asset bytes specification in the reload syntax.
constexpr std::string_view asset_bytes_token = " bytes ";

// Line protocol separators: space between fields, newline for message boundary.
constexpr char protocol_space_separator = ' ';
constexpr char protocol_newline_terminator = '\n';

// Value "1" in protocol fields: true for boolean fields like entry_reloadable, effects present, resident.
constexpr std::string_view protocol_value_true = "1";

// Key-value separator in error messages shown to the user.
constexpr std::string_view error_message_separator = ": ";

// Status message field names (src/rv_pconsole/rv_pconsole_cmd_devtools.cpp).
constexpr std::string_view msg_key_disc = "disc";
constexpr std::string_view msg_key_disc_hash = "disc_hash";
constexpr std::string_view msg_key_pdk = "pdk";
constexpr std::string_view msg_key_medium = "medium";
constexpr std::string_view msg_key_entry_reloadable = "entry_reloadable";
constexpr std::string_view msg_key_entry_revision = "entry_revision";
constexpr std::string_view msg_key_entry_hash = "entry_hash";
constexpr std::string_view msg_key_lua_used = "lua_used";
constexpr std::string_view msg_key_lua_budget = "lua_budget";

// Reply field names for errors and reload results (src/rv_pconsole/rv_pconsole_cmd_devtools.cpp).
constexpr std::string_view msg_key_error = "error";
constexpr std::string_view msg_key_message = "msg";
constexpr std::string_view msg_key_effects = "effects";
constexpr std::string_view msg_key_hash = "hash";
constexpr std::string_view msg_key_resident = "resident";
constexpr std::string_view msg_key_width = "width";
constexpr std::string_view msg_key_height = "height";

// Texture dimensions separator in asset reload results.
constexpr char texture_dimensions_separator = 'x';

// Protocol trace marker for outbound messages in the log.
constexpr std::string_view protocol_sent_marker = "> ";

// Bytes in a KiB, for the size shown to the user.
constexpr size_t bytes_per_kib = 1024;

int64_t rv_editor_field_int(const rv_editor_devmsg &msg, std::string_view key, int64_t fallback)
{
    const std::string_view s = msg.get(key);
    int64_t v = fallback;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

// What a reload-slot verb ("reload entry", "reload module <name>" or
// "asset <name> bytes <n>") is about; false for any other verb.
bool rv_editor_parse_reload_verb(const std::string &verb, rv_editor_reload_kind &kind, std::string &target)
{
    if (verb == reload_entry_verb) {
        kind = rv_editor_reload_kind::entry;
        target.clear();
        return true;
    }
    if (verb.starts_with(reload_module_prefix)) {
        kind = rv_editor_reload_kind::module;
        target = verb.substr(reload_module_prefix.size());
        return true;
    }
    if (verb.starts_with(asset_verb_prefix)) {
        const std::string rest = verb.substr(asset_verb_prefix.size());
        const size_t at = rest.rfind(asset_bytes_token);
        if (at == std::string::npos) {
            return false;
        }
        kind = rv_editor_reload_kind::texture;
        target = rest.substr(0, at);
        return true;
    }
    return false;
}

} // namespace

int64_t rv_editor_session::send(const std::string &verb, rv_editor_log &log, std::string_view payload)
{
    const int64_t id = next_id_++;
    const std::string line = std::to_string(id) + std::string(1, protocol_space_separator) + verb +
        std::string(1, protocol_newline_terminator);
    std::string wire = line;
    wire.append(payload);
    if (proc_.write(wire) != RV_OK) {
        if (!proc_.stdin_open()) {
            log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
                "cannot send '" + verb + "': the runtime's input is closed", rv_editor_log_channel::none,
                proc_.pid(), number_);
        } else if (!input_full_) {
            // Said once; pad keeps the newest state and tries again every frame.
            input_full_ = true;
            log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
                "cannot send '" + verb + "': the runtime is not reading its input (" +
                    std::to_string(rv_editor_process::input_max / bytes_per_kib) + " KiB waiting)",
                rv_editor_log_channel::none, proc_.pid(), number_);
        }
        return 0;
    }
    if (input_full_) {
        input_full_ = false;
        log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "the runtime reads its input again",
            rv_editor_log_channel::none, proc_.pid(), number_);
    }
    pending_[id] = { verb, std::chrono::steady_clock::now(), false, state_ == rv_editor_run_state::paused, frame_ };
    // The protocol trace shows the header only: a payload never belongs in the log.
    log.add(rv_editor_log_source::protocol, rv_editor_log_level::info,
        std::string(protocol_sent_marker) + line.substr(0, line.size() - 1), rv_editor_log_channel::none,
        proc_.pid(), number_);
    return id;
}

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
    const std::string verb = module.empty() ? std::string(reload_entry_verb) :
                                              std::string(reload_module_prefix) + module;
    const int64_t id = send(verb, log);
    if (id != 0) {
        reloading_ = true;
        reload_id_ = id;
    }
}

void rv_editor_session::reload_asset(rv_editor_log &log, const std::string &name, const std::vector<unsigned char> &bytes)
{
    if (!handshake_done_ || !proc_.running() || quit_sent_ || reloading_) {
        return;
    }
    if (bytes.size() > asset_payload_max) {
        log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
            "cannot send asset '" + name + "': " + std::to_string(bytes.size()) +
                " bytes over the console's payload ceiling of " + std::to_string(asset_payload_max),
            rv_editor_log_channel::none, proc_.pid(), number_);
        return;
    }
    const std::string verb = std::string(asset_verb_prefix) + name + std::string(asset_bytes_token) +
        std::to_string(bytes.size());
    const size_t header_size = std::to_string(next_id_).size() + 1 + verb.size() + 1;
    if (header_size + bytes.size() > rv_editor_process::input_max) {
        log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
            "cannot send asset '" + name + "': " + std::to_string(header_size + bytes.size()) +
                " bytes over the runtime's input limit of " + std::to_string(rv_editor_process::input_max),
            rv_editor_log_channel::none, proc_.pid(), number_);
        return;
    }
    const int64_t id =
        send(verb, log, std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
    if (id != 0) {
        reloading_ = true;
        reload_id_ = id;
    }
}

void rv_editor_session::refresh(rv_editor_log &log)
{
    if (handshake_done_ && proc_.running() && !quit_sent_) {
        send(std::string(cmd_status), log);
    }
}

void rv_editor_session::note_facts(const rv_editor_devmsg &msg)
{
    facts_.disc = rv_editor_hex_decode(msg.get(msg_key_disc));
    facts_.code_hash = msg.get(msg_key_disc_hash);
    facts_.pdk = msg.get(msg_key_pdk);
    facts_.medium = msg.get(msg_key_medium);
    facts_.reloadable = msg.get(msg_key_entry_reloadable) == protocol_value_true;
    facts_.revision = rv_editor_field_int(msg, msg_key_entry_revision, facts_.revision);
    if (facts_.first_revision < 0) {
        facts_.first_revision = facts_.revision;
    }
    facts_.entry_hash = msg.get(msg_key_entry_hash);
    facts_.lua_used = rv_editor_field_int(msg, msg_key_lua_used, facts_.lua_used);
    facts_.lua_budget = rv_editor_field_int(msg, msg_key_lua_budget, facts_.lua_budget);
    facts_.at = std::chrono::system_clock::now();
}

bool rv_editor_session::handle_query(const rv_editor_request &req, const rv_editor_devmsg &msg, rv_editor_log &log)
{
    const bool err = msg.kind == rv_editor_devmsg::rv_editor_devmsg_kind::err;
    if (req.verb.starts_with(cmd_get_prefix) || req.verb.starts_with(cmd_keys_prefix) || req.verb == cmd_keys_exact) {
        // An answer describes the frame it was asked on only when the machine was
        // paused then: nothing ran between the question and the reading.
        rv_editor_answer &a = answers_[req.verb];
        a.ok = !err;
        a.fields = msg.fields;
        a.error = err ? std::string(msg.get(msg_key_error)) + std::string(error_message_separator) +
                rv_editor_hex_decode(msg.get(msg_key_message)) :
                        std::string();
        a.frame_exact = req.paused;
        a.frame = req.frame;
        a.at = std::chrono::system_clock::now();
        return true;
    }
    rv_editor_reload_kind kind = rv_editor_reload_kind::entry;
    std::string target;
    if (!rv_editor_parse_reload_verb(req.verb, kind, target)) {
        return false;
    }
    // A late answer for a superseded reload (msg.id != reload_id_) is logged under
    // its own target, but the flag and the shown result belong to the current one.
    const bool current = msg.id == reload_id_;
    if (current) {
        reloading_ = false;
        reload_target_ = target;
        reload_kind_ = kind;
    }
    const std::string named = kind == rv_editor_reload_kind::entry ? std::string()
        : kind == rv_editor_reload_kind::module ? "module " + target + ": "
                                                 : "asset " + target + ": ";
    if (err) {
        // The previous code stays; whether its effects ran is the runtime's word.
        const std::string result = std::string(msg.get(msg_key_error)) + std::string(error_message_separator) +
            rv_editor_hex_decode(msg.get(msg_key_message)) +
            (msg.get(msg_key_effects) == protocol_value_true ? " (effects may have happened before it failed)" : "");
        log.add(rv_editor_log_source::runtime, rv_editor_log_level::error, "reload refused: " + named + result);
        if (current) {
            reload_ok_ = false;
            reload_result_ = result;
        }
        if (proc_.running()) {
            send(std::string(cmd_status), log);
        }
        return true;
    }
    std::string result;
    if (kind == rv_editor_reload_kind::entry) {
        facts_.revision = rv_editor_field_int(msg, msg_key_entry_revision, facts_.revision);
        facts_.entry_hash = msg.get(msg_key_entry_hash);
        facts_.lua_used = rv_editor_field_int(msg, msg_key_lua_used, facts_.lua_used);
        facts_.at = std::chrono::system_clock::now();
        result = "entry revision " + std::to_string(facts_.revision);
    } else if (kind == rv_editor_reload_kind::module) {
        result = "module " + target + ", hash " + std::string(msg.get(msg_key_hash));
    } else if (msg.get(msg_key_resident) == protocol_value_true) {
        result = "asset " + target + ", " + std::string(msg.get(msg_key_width)) + texture_dimensions_separator +
            std::string(msg.get(msg_key_height));
    } else {
        result = "asset " + target + ", nothing resident to refresh";
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
    rv_editor_reload_kind kind = rv_editor_reload_kind::entry;
    std::string target;
    if (id != reload_id_ || !rv_editor_parse_reload_verb(verb, kind, target)) {
        return;
    }
    reloading_ = false;
    reload_target_ = target;
    reload_kind_ = kind;
    reload_ok_ = false;
    reload_result_ = "no answer: whether it applied is unknown";
}

} // namespace rv_editor
