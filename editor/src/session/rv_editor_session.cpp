// The runtime session: one development console, driven over its protocol.

#include "session/rv_editor_session.hpp"

#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// A request unanswered this long is reported as of unknown effect; the first
// status gets longer, because the console loads its disc before answering.
constexpr auto rv_editor_request_timeout = std::chrono::seconds(5);
constexpr auto rv_editor_handshake_timeout = std::chrono::seconds(15);
// After quit, a console still running this long counts as hung.
constexpr auto rv_editor_stop_grace = std::chrono::seconds(3);

constexpr std::string_view pad_prefix = "pad 0 ";
constexpr std::string_view protocol_frame_event = "0 event=frame ";

constexpr int fd_frame_descriptor = 3;
constexpr size_t hex_buffer_size = std::numeric_limits<uint64_t>::digits / 4 + 1; // 16 hex digits + NUL
constexpr size_t read_buffer_size = 1 << 20;
constexpr auto channel_lost_timeout = std::chrono::milliseconds(500);
constexpr auto shutdown_timeout = std::chrono::seconds(2);
constexpr auto shutdown_grace_timeout = std::chrono::milliseconds(2500);
constexpr auto shutdown_poll_interval = std::chrono::milliseconds(20);
constexpr int exit_code_player_build = 2;

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
    return active_;
}

bool rv_editor_session::hung() const
{
    return proc_.running() && quit_sent_ && std::chrono::steady_clock::now() - stop_sent_ > rv_editor_stop_grace;
}

int rv_editor_session::start(const std::filesystem::path &console, const std::filesystem::path &disc_dir,
    const std::filesystem::path &memcard, const std::filesystem::path &cwd, uint32_t build_number,
    const std::vector<std::string> &options, const std::vector<std::string> &env, rv_editor_log &log,
    std::string &error)
{
    if (live()) {
        error = "a runtime is already running in this window";
        return RV_ERR_BUSY;
    }
    const int err_frame = frame_mem_.create(error);
    if (err_frame != RV_OK) {
        return err_frame;
    }
    // The console writes its frames into frame_mem_, handed over as the frame descriptor.
    std::vector<std::string> argv = { console.string(), "--dev", "--frame-fd", std::to_string(fd_frame_descriptor),
        "--memcard", memcard.string() };
    argv.insert(argv.end(), options.begin(), options.end());
    argv.push_back(disc_dir.string());
    const int err_proc = proc_.start(argv, cwd, error, frame_mem_.fd(), env);
    if (err_proc != RV_OK) {
        return err_proc;
    }

    parser_ = {};
    pending_.clear();
    next_id_ = 1;
    frame_ = 0;
    pad_sent_ = 0;
    uncertain_ = false;
    quit_sent_ = false;
    forced_ = false;
    input_full_ = false;
    handshake_done_ = false;
    channel_open_ = true;
    build_number_ = build_number;
    ++number_;
    reloading_ = false;
    reload_id_ = 0;
    reload_result_.clear();
    facts_ = {};
    answers_.clear();
    scenes_read_.clear();
    started_wall_ = std::chrono::system_clock::now();
    ended_wall_ = {};
    disc_dir_ = disc_dir;
    console_ = console;
    last_confirmed_ = {};
    in_flight_.clear();
    end_reason_.clear();
    refusal_.clear();
    eof_at_ = {};
    err_partial_.clear();
    out_partial_.clear();
    started_ = std::chrono::steady_clock::now();
    state_ = rv_editor_run_state::starting;
    active_ = true;
    log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
        "runtime started, pid " + std::to_string(proc_.pid()) + ": " + console.string() + " --dev --frame-fd 3 --memcard " +
            memcard.string() + " " + disc_dir.string(), rv_editor_log_channel::none, proc_.pid(), number_);
    // Nothing is enabled until this answers.
    send(std::string(cmd_status), log);
    return RV_OK;
}

void rv_editor_session::trace(std::string_view bytes, rv_editor_log &log)
{
    // Sixty frame events a second would push every other line out of the bounded
    // log; the frame number they carry is in the session's state instead.
    out_partial_.append(bytes);
    size_t start = 0;
    for (size_t nl = out_partial_.find('\n'); nl != std::string::npos; nl = out_partial_.find('\n', start)) {
        const std::string_view line(out_partial_.data() + start, nl - start);
        if (!line.starts_with(protocol_frame_event)) {
            log.add(rv_editor_log_source::protocol, rv_editor_log_level::info, line, rv_editor_log_channel::out,
                proc_.pid(), number_);
        }
        start = nl + 1;
    }
    out_partial_.erase(0, start);
    if (out_partial_.size() > rv_editor_log::line_max) {
        log.add(rv_editor_log_source::protocol, rv_editor_log_level::info, out_partial_, rv_editor_log_channel::out,
            proc_.pid(), number_);
        out_partial_.clear();
    }
}

void rv_editor_session::pause(rv_editor_log &log)
{
    if (state_ == rv_editor_run_state::running && proc_.running() && send(std::string(cmd_pause), log) != 0) {
        state_ = rv_editor_run_state::pausing;
    }
}

void rv_editor_session::resume(rv_editor_log &log)
{
    if (state_ == rv_editor_run_state::paused && proc_.running() && send(std::string(cmd_resume), log) != 0) {
        state_ = rv_editor_run_state::resuming;
    }
}

void rv_editor_session::step(rv_editor_log &log)
{
    if (state_ == rv_editor_run_state::paused && proc_.running() && send(std::string(cmd_step), log) != 0) {
        state_ = rv_editor_run_state::stepping;
    }
}

void rv_editor_session::stop(rv_editor_log &log)
{
    if (!proc_.running() || quit_sent_) {
        return;
    }
    quit_sent_ = true;
    stop_sent_ = std::chrono::steady_clock::now();
    state_ = rv_editor_run_state::stopping;
    if (send(std::string(cmd_quit), log) == 0 && !proc_.stdin_open()) {
        // Nobody reads the channel any more: the process can only be ended. One
        // that only stopped reading shows as hung and gets Force Stop.
        force_stop(log);
    }
}

void rv_editor_session::force_stop(rv_editor_log &log)
{
    if (!proc_.running()) {
        return;
    }
    forced_ = true;
    log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
        "force-stopping runtime pid " + std::to_string(proc_.pid()), rv_editor_log_channel::none, proc_.pid(),
        number_);
    proc_.stop(true);
}

void rv_editor_session::pad(uint64_t buttons, rv_editor_log &log)
{
    if (!handshake_done_ || !proc_.running() || quit_sent_ || buttons == pad_sent_) {
        return;
    }
    char hex[hex_buffer_size];
    std::snprintf(hex, sizeof(hex), "%llx", static_cast<unsigned long long>(buttons));
    if (send(std::string(pad_prefix) + hex, log) != 0) {
        pad_sent_ = buttons;
    }
}

void rv_editor_session::update(rv_editor_log &log)
{
    // A live session keeps reading every frame, exited console or not: finish()
    // runs once, only when the console's output is complete (output_done()).
    if (!live()) {
        return;
    }

    std::string out;
    std::string err;
    proc_.read(out, err, read_buffer_size);
    proc_.flush();
    log.add_stream(rv_editor_log_source::runtime, err_partial_, err, rv_editor_log_channel::err, proc_.pid(),
        number_);
    if (!out.empty()) {
        trace(out, log);
        std::vector<rv_editor_devmsg> msgs;
        std::vector<std::string> errors;
        parser_.feed(out, msgs, errors);
        for (const std::string &e : errors) {
            log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "protocol: " + e,
                rv_editor_log_channel::none, proc_.pid(), number_);
        }
        for (const rv_editor_devmsg &m : msgs) {
            handle(m, log);
        }
    }

    // Everything below needs the console process itself still running: an exited
    // console gets no channel-lost check, no timeout and no send.
    if (proc_.running()) {
        const auto now = std::chrono::steady_clock::now();
        // A console that exits closes stdout a moment before it can be reaped; only a
        // process still alive a while after end of file has really lost its channel.
        if (channel_open_ && !proc_.stdout_open() && eof_at_ == std::chrono::steady_clock::time_point{}) {
            eof_at_ = now;
        }
        if (channel_open_ && eof_at_ != std::chrono::steady_clock::time_point{} &&
            now - eof_at_ > channel_lost_timeout) {
            channel_open_ = false;
            if (!quit_sent_) {
                state_ = rv_editor_run_state::disconnected;
                end_reason_ = "the protocol channel closed while the process still runs";
                log.add(rv_editor_log_source::editor, rv_editor_log_level::error, end_reason_,
                    rv_editor_log_channel::none, proc_.pid(), number_);
            }
        }

        // A timeout proves nothing about whether the request ran: say so,
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
                log.add(rv_editor_log_source::editor, rv_editor_log_level::error, refusal_,
                    rv_editor_log_channel::none, proc_.pid(), number_);
                force_stop(log);
                break;
            }
            uncertain_ = true;
            // Whether it ran is unknown; the user may ask again, the editor never does.
            note_reload_timeout(id, req.verb);
            ask_status = ask_status || (req.verb != "status" && req.verb != "quit");
            log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
                "no answer to '" + req.verb + "' after " + std::to_string(rv_editor_request_timeout.count()) +
                    " s: whether it ran is unknown", rv_editor_log_channel::none, proc_.pid(), number_);
        }
        if (ask_status && channel_open_) {
            send(std::string(cmd_status), log);
        }

        if (state_ == rv_editor_run_state::refused && hung()) {
            force_stop(log);
        }
    }

    if (proc_.poll() && proc_.output_done()) {
        finish(log);
    }
}

void rv_editor_session::finish(rv_editor_log &log)
{
    std::string out;
    std::string err;
    proc_.read(out, err, read_buffer_size);
    log.add_stream(rv_editor_log_source::runtime, err_partial_, err, rv_editor_log_channel::err, proc_.pid(),
        number_);
    log.flush_stream(rv_editor_log_source::runtime, err_partial_, rv_editor_log_channel::err, proc_.pid(), number_);
    trace(out, log);
    log.flush_stream(rv_editor_log_source::protocol, out_partial_, rv_editor_log_channel::out, proc_.pid(), number_);
    if (proc_.output_cut()) {
        log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
            "runtime output after exit was not fully read: a process it started kept a pipe open past the grace",
            rv_editor_log_channel::none, proc_.pid(), number_);
    }
    in_flight_.clear();
    for (const auto &[id, req] : pending_) {
        in_flight_.push_back({ req.verb, req.overdue });
    }
    pending_.clear();
    channel_open_ = false;
    active_ = false;

    ended_wall_ = std::chrono::system_clock::now();
    const rv_editor_process::rv_editor_exit &exit = proc_.exit_status();
    const std::string how = rv_editor_exit_text(exit);
    if (!refusal_.empty()) {
        state_ = rv_editor_run_state::refused;
        end_reason_ = refusal_;
    } else if (!handshake_done_) {
        state_ = rv_editor_run_state::refused;
        if (exit.signal == 0 && exit.code == exit_code_player_build) {
            end_reason_ = "exit code 2 before answering status: a player build does not know --dev";
        } else {
            end_reason_ = how + " before answering status";
        }
    } else if (forced_) {
        state_ = rv_editor_run_state::exited;
        end_reason_ = "force-stopped (" + how + ")";
    } else if (exit.signal != 0 || exit.code != 0) {
        state_ = rv_editor_run_state::crashed;
        end_reason_ = how;
    } else {
        state_ = rv_editor_run_state::exited;
        end_reason_ = quit_sent_ ? "stopped" : "the console ended by itself";
    }
    log.add(rv_editor_log_source::editor,
        state_ == rv_editor_run_state::crashed || state_ == rv_editor_run_state::refused ? rv_editor_log_level::error
                                                                                         : rv_editor_log_level::info,
        "runtime pid " + std::to_string(proc_.pid()) + " ended: " + end_reason_, rv_editor_log_channel::none,
        proc_.pid(), number_);
}

void rv_editor_session::shutdown(rv_editor_log &log)
{
    if (!live()) {
        return;
    }
    stop(log);
    const auto until = std::chrono::steady_clock::now() + shutdown_timeout;
    while (live() && proc_.running() && std::chrono::steady_clock::now() < until) {
        update(log);
        std::this_thread::sleep_for(shutdown_poll_interval);
    }
    if (proc_.running()) {
        force_stop(log);
    }
    // Bounded even when a grandchild keeps a pipe open past output_done()'s grace:
    // shutdown never hangs.
    const auto grace_until = std::chrono::steady_clock::now() + shutdown_grace_timeout;
    while (live() && std::chrono::steady_clock::now() < grace_until) {
        update(log);
        std::this_thread::sleep_for(shutdown_poll_interval);
    }
    if (live()) {
        finish(log);
    }
}

} // namespace rv_editor
