// The runtime session: one development console, driven over its protocol.

#include "session/rv_editor_session.hpp"

#include <charconv>
#include <thread>
#include <vector>

namespace rv_editor
{

namespace
{

// A request unanswered this long is reported as of unknown effect; the first
// status gets longer, because the console loads its disc before answering.
constexpr auto rv_editor_request_timeout = std::chrono::seconds(5);
constexpr auto rv_editor_handshake_timeout = std::chrono::seconds(15);
// After quit, a console still running this long counts as hung (DEV-09).
constexpr auto rv_editor_stop_grace = std::chrono::seconds(3);

int64_t rv_editor_to_int(std::string_view s, int64_t fallback)
{
    int64_t v = fallback;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

} // namespace

const char *rv_editor_run_state_name(rv_editor_run_state state)
{
    switch (state) {
        case rv_editor_run_state::stopped: return "Stopped";
        case rv_editor_run_state::starting: return "Starting";
        case rv_editor_run_state::running: return "Running";
        case rv_editor_run_state::pausing: return "Pausing";
        case rv_editor_run_state::paused: return "Paused";
        case rv_editor_run_state::stepping: return "Stepping";
        case rv_editor_run_state::resuming: return "Resuming";
        case rv_editor_run_state::stopping: return "Stopping";
        case rv_editor_run_state::exited: return "Exited";
        case rv_editor_run_state::crashed: return "Crashed";
        case rv_editor_run_state::disconnected: return "Disconnected";
        case rv_editor_run_state::refused: return "Refused";
    }
    return "?";
}

bool rv_editor_session::live() const
{
    return proc_.running();
}

bool rv_editor_session::hung() const
{
    return live() && quit_sent_ && std::chrono::steady_clock::now() - stop_sent_ > rv_editor_stop_grace;
}

bool rv_editor_session::start(const std::filesystem::path &console, const std::filesystem::path &disc_dir,
    const std::filesystem::path &memcard, const std::filesystem::path &cwd, uint32_t build_number, rv_editor_log &log,
    std::string &error)
{
    if (live()) {
        error = "a runtime is already running in this window";
        return false;
    }
    const std::vector<std::string> argv = { console.string(), "--dev", "--memcard", memcard.string(),
        disc_dir.string() };
    if (!proc_.start(argv, cwd, error)) {
        return false;
    }

    parser_ = {};
    pending_.clear();
    next_id_ = 1;
    frame_ = 0;
    uncertain_ = false;
    quit_sent_ = false;
    forced_ = false;
    handshake_done_ = false;
    channel_open_ = true;
    build_number_ = build_number;
    disc_dir_ = disc_dir;
    end_reason_.clear();
    refusal_.clear();
    eof_at_ = {};
    err_partial_.clear();
    out_partial_.clear();
    started_ = std::chrono::steady_clock::now();
    state_ = rv_editor_run_state::starting;
    log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
        "runtime started, pid " + std::to_string(proc_.pid()) + ": " + console.string() + " --dev --memcard " +
            memcard.string() + " " + disc_dir.string());
    // Nothing is enabled until this answers (DEV-02).
    send("status", log);
    return true;
}

int64_t rv_editor_session::send(const std::string &verb, rv_editor_log &log)
{
    const int64_t id = next_id_++;
    const std::string line = std::to_string(id) + " " + verb + "\n";
    if (!proc_.write(line)) {
        log.add(rv_editor_log_source::editor, rv_editor_log_level::error,
            "cannot send '" + verb + "': the runtime's input is closed");
        return 0;
    }
    pending_[id] = { verb, std::chrono::steady_clock::now(), false };
    log.add(rv_editor_log_source::protocol, rv_editor_log_level::info, "> " + line.substr(0, line.size() - 1));
    return id;
}

void rv_editor_session::pause(rv_editor_log &log)
{
    if (state_ == rv_editor_run_state::running && send("pause", log) != 0) {
        state_ = rv_editor_run_state::pausing;
    }
}

void rv_editor_session::resume(rv_editor_log &log)
{
    if (state_ == rv_editor_run_state::paused && send("resume", log) != 0) {
        state_ = rv_editor_run_state::resuming;
    }
}

void rv_editor_session::step(rv_editor_log &log)
{
    if (state_ == rv_editor_run_state::paused && send("step", log) != 0) {
        state_ = rv_editor_run_state::stepping;
    }
}

void rv_editor_session::stop(rv_editor_log &log)
{
    if (!live() || quit_sent_) {
        return;
    }
    quit_sent_ = true;
    stop_sent_ = std::chrono::steady_clock::now();
    state_ = rv_editor_run_state::stopping;
    if (send("quit", log) == 0) {
        // Nobody reads the channel any more: the process can only be ended.
        force_stop(log);
    }
}

void rv_editor_session::force_stop(rv_editor_log &log)
{
    if (!live()) {
        return;
    }
    forced_ = true;
    log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
        "force-stopping runtime pid " + std::to_string(proc_.pid()));
    proc_.stop(true);
}

void rv_editor_session::handle_mode(std::string_view mode)
{
    if (mode == "paused") {
        state_ = rv_editor_run_state::paused;
    } else if (mode == "running") {
        state_ = rv_editor_run_state::running;
    } else if (mode == "stopped") {
        state_ = rv_editor_run_state::stopping;
    }
}

void rv_editor_session::handle(const rv_editor_devmsg &msg, rv_editor_log &log)
{
    if (msg.has("frame")) {
        frame_ = rv_editor_to_int(msg.get("frame"), frame_);
    }

    if (msg.kind == rv_editor_devmsg::rv_editor_devmsg_kind::event) {
        const std::string_view event = msg.get("event");
        if (event == "pause") {
            handle_mode(msg.get("mode"));
        } else if (event == "script_error") {
            state_ = rv_editor_run_state::paused;
            log.add(rv_editor_log_source::runtime, rv_editor_log_level::error,
                "script error at frame " + std::string(msg.get("frame")) + ", machine paused: " +
                    rv_editor_hex_decode(msg.get("msg")));
        }
        return;
    }

    const auto it = pending_.find(msg.id);
    if (it == pending_.end()) {
        log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
            "answer to request " + std::to_string(msg.id) + ", which was never sent");
        return;
    }
    const std::string verb = it->second.verb;
    pending_.erase(it);

    if (msg.kind == rv_editor_devmsg::rv_editor_devmsg_kind::err) {
        log.add(rv_editor_log_source::runtime, rv_editor_log_level::error,
            verb + " refused: " + std::string(msg.get("error")) + ": " + rv_editor_hex_decode(msg.get("msg")));
        // What the machine does now is whatever it says it does.
        if (state_ != rv_editor_run_state::stopping) {
            send("status", log);
        }
        return;
    }

    if (verb == "status") {
        uncertain_ = false;
        if (!handshake_done_) {
            const int64_t protocol = rv_editor_to_int(msg.get("protocol"), -1);
            if (protocol != protocol_supported) {
                state_ = rv_editor_run_state::refused;
                refusal_ = "the console speaks protocol " + std::string(msg.get("protocol")) +
                    "; this editor speaks " + std::to_string(protocol_supported);
                end_reason_ = refusal_;
                log.add(rv_editor_log_source::editor, rv_editor_log_level::error, refusal_);
                quit_sent_ = true;
                stop_sent_ = std::chrono::steady_clock::now();
                send("quit", log);
                return;
            }
            handshake_done_ = true;
            log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
                "connected: protocol " + std::string(msg.get("protocol")) + ", disc " +
                    rv_editor_hex_decode(msg.get("disc")) + ", pdk " + std::string(msg.get("pdk")) + ", medium " +
                    std::string(msg.get("medium")));
        }
        if (state_ != rv_editor_run_state::stopping) {
            handle_mode(msg.get("mode"));
        }
        return;
    }
    if (verb == "pause" || verb == "resume" || verb == "step") {
        const std::string_view mode = msg.get("mode");
        if (mode != "paused" && mode != "running") {
            // An answer that does not say where the machine is: ask.
            send("status", log);
            return;
        }
        handle_mode(mode);
        return;
    }
    if (verb == "quit") {
        state_ = rv_editor_run_state::stopping;
    }
}

void rv_editor_session::update(rv_editor_log &log)
{
    // An ended process was finished on the update that noticed it.
    if (!live()) {
        return;
    }

    std::string out;
    std::string err;
    proc_.read(out, err, 1 << 20);
    proc_.flush();
    log.add_stream(rv_editor_log_source::runtime, err_partial_, err);
    if (!out.empty()) {
        log.add_stream(rv_editor_log_source::protocol, out_partial_, out);
        std::vector<rv_editor_devmsg> msgs;
        std::vector<std::string> errors;
        parser_.feed(out, msgs, errors);
        for (const std::string &e : errors) {
            log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "protocol: " + e);
        }
        for (const rv_editor_devmsg &m : msgs) {
            handle(m, log);
        }
    }

    const auto now = std::chrono::steady_clock::now();
    // A console that exits closes stdout a moment before it can be reaped; only a
    // process still alive a while after end of file has really lost its channel.
    if (live() && channel_open_ && !proc_.stdout_open() && eof_at_ == std::chrono::steady_clock::time_point{}) {
        eof_at_ = now;
    }
    if (live() && channel_open_ && eof_at_ != std::chrono::steady_clock::time_point{} &&
        now - eof_at_ > std::chrono::milliseconds(500)) {
        channel_open_ = false;
        if (!quit_sent_) {
            state_ = rv_editor_run_state::disconnected;
            end_reason_ = "the protocol channel closed while the process still runs";
            log.add(rv_editor_log_source::editor, rv_editor_log_level::error, end_reason_);
        }
    }

    // A timeout proves nothing about whether the request ran (DEV-07): say so,
    // ask for status, and never resend the request itself.
    bool ask_status = false;
    for (auto &[id, req] : pending_) {
        const auto limit = !handshake_done_ && req.verb == "status" ? rv_editor_handshake_timeout
                                                                    : rv_editor_request_timeout;
        if (req.overdue || now - req.sent < limit) {
            continue;
        }
        req.overdue = true;
        if (!handshake_done_) {
            state_ = rv_editor_run_state::refused;
            refusal_ = "no answer to status: not a development console, or it could not load the disc";
            end_reason_ = refusal_;
            log.add(rv_editor_log_source::editor, rv_editor_log_level::error, refusal_);
            force_stop(log);
            break;
        }
        uncertain_ = true;
        ask_status = ask_status || (req.verb != "status" && req.verb != "quit");
        log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
            "no answer to '" + req.verb + "' after " + std::to_string(rv_editor_request_timeout.count()) +
                " s: whether it ran is unknown");
    }
    if (ask_status && live() && channel_open_) {
        send("status", log);
    }

    if (state_ == rv_editor_run_state::refused && live() && hung()) {
        force_stop(log);
    }
    if (proc_.poll()) {
        finish(log);
    }
}

void rv_editor_session::finish(rv_editor_log &log)
{
    std::string out;
    std::string err;
    proc_.read(out, err, 1 << 20);
    log.add_stream(rv_editor_log_source::runtime, err_partial_, err);
    log.flush_stream(rv_editor_log_source::runtime, err_partial_);
    log.add_stream(rv_editor_log_source::protocol, out_partial_, out);
    log.flush_stream(rv_editor_log_source::protocol, out_partial_);
    pending_.clear();
    channel_open_ = false;

    const rv_editor_process::rv_editor_exit &exit = proc_.exit_status();
    const std::string how = rv_editor_exit_text(exit);
    if (!refusal_.empty()) {
        state_ = rv_editor_run_state::refused;
        end_reason_ = refusal_;
    } else if (!handshake_done_) {
        state_ = rv_editor_run_state::refused;
        end_reason_ = exit.signal == 0 && exit.code == 2
            ? "exit code 2 before answering status: a player build does not know --dev"
            : how + " before answering status";
    } else if (forced_) {
        state_ = rv_editor_run_state::exited;
        end_reason_ = "force-stopped (" + how + ")";
    } else if (exit.signal != 0 || exit.code != 0) {
        state_ = rv_editor_run_state::crashed;
        end_reason_ = how;
    } else {
        state_ = rv_editor_run_state::exited;
        end_reason_ = quit_sent_ ? "stopped" : "the console ended by itself (its window was closed)";
    }
    log.add(rv_editor_log_source::editor,
        state_ == rv_editor_run_state::crashed || state_ == rv_editor_run_state::refused ? rv_editor_log_level::error
                                                                                         : rv_editor_log_level::info,
        "runtime pid " + std::to_string(proc_.pid()) + " ended: " + end_reason_);
}

void rv_editor_session::shutdown(rv_editor_log &log)
{
    if (!live()) {
        return;
    }
    stop(log);
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (live() && std::chrono::steady_clock::now() < until) {
        update(log);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (live()) {
        force_stop(log);
        while (!proc_.poll()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        finish(log);
    }
}

} // namespace rv_editor
