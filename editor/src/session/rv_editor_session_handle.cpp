// The session's dev-protocol message handling: events and request replies.

#include "session/rv_editor_session.hpp"

#include <charconv>
#include <string>
#include <string_view>

namespace rv_editor
{

namespace
{

int64_t rv_editor_to_int(std::string_view s, int64_t fallback)
{
    int64_t v = fallback;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

// Dev-protocol state words.
constexpr std::string_view devproto_mode_paused = "paused";
constexpr std::string_view devproto_mode_running = "running";
constexpr std::string_view devproto_mode_stopped = "stopped";

// Dev-protocol message field names.
constexpr std::string_view devproto_field_frame = "frame";
constexpr std::string_view devproto_field_mode = "mode";
constexpr std::string_view devproto_field_protocol = "protocol";
constexpr std::string_view devproto_field_event = "event";
constexpr std::string_view devproto_field_name = "name";

// Dev-protocol event names.
constexpr std::string_view devproto_event_pause = "pause";
constexpr std::string_view devproto_event_scene = "scene";
constexpr std::string_view devproto_event_script_error = "script_error";

} // namespace

void rv_editor_session::handle_mode(std::string_view mode)
{
    if (mode == devproto_mode_paused) {
        state_ = rv_editor_run_state::paused;
    } else if (mode == devproto_mode_running) {
        state_ = rv_editor_run_state::running;
    } else if (mode == devproto_mode_stopped) {
        state_ = rv_editor_run_state::stopping;
    }
}

void rv_editor_session::handle(const rv_editor_devmsg &msg, rv_editor_log &log)
{
    if (msg.has(devproto_field_frame)) {
        frame_ = rv_editor_to_int(msg.get(devproto_field_frame), frame_);
    }

    if (msg.kind == rv_editor_devmsg::rv_editor_devmsg_kind::event) {
        const std::string_view event = msg.get(devproto_field_event);
        if (event == devproto_event_pause) {
            handle_mode(msg.get(devproto_field_mode));
        } else if (event == devproto_event_scene) {
            const std::string name = rv_editor_hex_decode(msg.get(devproto_field_name));
            if (!name.empty()) {
                scenes_read_.insert(name);
            }
        } else if (event == devproto_event_script_error) {
            state_ = rv_editor_run_state::paused;
            log.add(rv_editor_log_source::runtime,
                rv_editor_log_level::error,
                "script error at frame " + std::string(msg.get(devproto_field_frame)) +
                    ", machine paused: " + rv_editor_hex_decode(msg.get(msg_key_message)),
                rv_editor_log_channel::none,
                proc_.pid(),
                number_);
        }
        return;
    }

    const auto it = pending_.find(msg.id);
    if (it == pending_.end()) {
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            "answer to request " + std::to_string(msg.id) + ", which was never sent",
            rv_editor_log_channel::none,
            proc_.pid(),
            number_);
        return;
    }
    const rv_editor_request req = it->second;
    const std::string &verb = req.verb;
    pending_.erase(it);
    if (msg.kind == rv_editor_devmsg::rv_editor_devmsg_kind::ok) {
        last_confirmed_ = { verb, frame_, std::chrono::system_clock::now() };
    }
    if (handle_query(req, msg, log)) {
        return;
    }

    if (msg.kind == rv_editor_devmsg::rv_editor_devmsg_kind::err) {
        const std::string err_msg =
            verb + " refused: " + std::string(msg.get(msg_key_error)) + ": " + rv_editor_hex_decode(msg.get(msg_key_message));
        log.add(rv_editor_log_source::runtime,
            rv_editor_log_level::error,
            err_msg,
            rv_editor_log_channel::none,
            proc_.pid(),
            number_);
        // What the machine does now is whatever it says it does.
        if (state_ != rv_editor_run_state::stopping && proc_.running()) {
            send(std::string(cmd_status), log);
        }
        return;
    }

    if (verb == cmd_status) {
        uncertain_ = false;
        if (!handshake_done_) {
            const std::string_view protocol = msg.get(devproto_field_protocol);
            if (protocol != protocol_supported) {
                state_ = rv_editor_run_state::refused;
                refusal_ =
                    "the console speaks protocol " + std::string(protocol) + "; this editor speaks " + protocol_supported;
                end_reason_ = refusal_;
                log.add(rv_editor_log_source::editor,
                    rv_editor_log_level::error,
                    refusal_,
                    rv_editor_log_channel::none,
                    proc_.pid(),
                    number_);
                quit_sent_ = true;
                stop_sent_ = std::chrono::steady_clock::now();
                if (proc_.running()) {
                    send(std::string(cmd_quit), log);
                }
                return;
            }
            handshake_done_ = true;
            const std::string handshake_msg = "connected: protocol " + std::string(msg.get(devproto_field_protocol)) +
                ", disc " + rv_editor_hex_decode(msg.get(msg_key_disc)) + ", pdk " + std::string(msg.get(msg_key_pdk)) +
                ", medium " + std::string(msg.get(msg_key_medium));
            log.add(rv_editor_log_source::editor,
                rv_editor_log_level::info,
                handshake_msg,
                rv_editor_log_channel::none,
                proc_.pid(),
                number_);
        }
        note_facts(msg);
        if (state_ != rv_editor_run_state::stopping) {
            handle_mode(msg.get(devproto_field_mode));
        }
        return;
    }
    if (verb == cmd_pause || verb == cmd_resume || verb == cmd_step) {
        const std::string_view mode = msg.get(devproto_field_mode);
        if (mode != devproto_mode_paused && mode != devproto_mode_running) {
            // An answer that does not say where the machine is: ask.
            if (proc_.running()) {
                send(std::string(cmd_status), log);
            }
            return;
        }
        handle_mode(mode);
        return;
    }
    if (verb == cmd_quit) {
        state_ = rv_editor_run_state::stopping;
    }
}

} // namespace rv_editor
