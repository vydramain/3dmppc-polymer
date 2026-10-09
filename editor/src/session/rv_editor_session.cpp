// The runtime session: one development console, driven over its protocol.

#include "session/rv_editor_session.hpp"

#include <cstdio>
#include <initializer_list>
#include <limits>
#include <vector>

#include "pdk/rv_err.h"
#include "text/rv_editor_text.hpp"

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

// Separates the failure texts of one error text.
constexpr std::string_view failure_separator = "; ";

// The first code that is not RV_OK, in order; RV_OK when all of them are.
int rv_editor_first_failure(std::initializer_list<int> codes)
{
    for (const int code : codes) {
        if (code != RV_OK) {
            return code;
        }
    }
    return RV_OK;
}

constexpr std::string_view pad_prefix = "pad 0 ";
constexpr std::string_view protocol_frame_event = "0 event=frame ";

constexpr size_t hex_buffer_size = std::numeric_limits<uint64_t>::digits / 4 + 1; // 16 hex digits + NUL
constexpr size_t read_buffer_size = 1 << 20;
constexpr auto channel_lost_timeout = std::chrono::milliseconds(500);
constexpr int exit_code_player_build = 2;

} // namespace

void rv_editor_append_failure(std::string &text, const std::string &part)
{
    if (!text.empty()) {
        text += failure_separator;
    }
    text += part;
}

const char *rv_editor_run_state_name(rv_editor_run_state state)
{
    switch (state) {
    case rv_editor_run_state::stopped:
        return rv_editor_text("run_state.stopped");
    case rv_editor_run_state::starting:
        return rv_editor_text("run_state.starting");
    case rv_editor_run_state::running:
        return rv_editor_text("run_state.running");
    case rv_editor_run_state::pausing:
        return rv_editor_text("run_state.pausing");
    case rv_editor_run_state::paused:
        return rv_editor_text("run_state.paused");
    case rv_editor_run_state::stepping:
        return rv_editor_text("run_state.stepping");
    case rv_editor_run_state::resuming:
        return rv_editor_text("run_state.resuming");
    case rv_editor_run_state::stopping:
        return rv_editor_text("run_state.stopping");
    case rv_editor_run_state::exited:
        return rv_editor_text("run_state.exited");
    case rv_editor_run_state::crashed:
        return rv_editor_text("run_state.crashed");
    case rv_editor_run_state::disconnected:
        return rv_editor_text("run_state.disconnected");
    case rv_editor_run_state::refused:
        return rv_editor_text("run_state.refused");
    }
    return rv_editor_text("run_state.unknown");
}

bool rv_editor_session::live() const
{
    return active_;
}

bool rv_editor_session::hung() const
{
    return proc_.running() && quit_sent_ && std::chrono::steady_clock::now() - stop_sent_ > rv_editor_stop_grace;
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
            log.add(rv_editor_log_source::protocol,
                rv_editor_log_level::info,
                line,
                rv_editor_log_channel::out,
                proc_.pid(),
                number_);
        }
        start = nl + 1;
    }
    out_partial_.erase(0, start);
    if (out_partial_.size() > rv_editor_log::line_max) {
        log.add(rv_editor_log_source::protocol,
            rv_editor_log_level::info,
            out_partial_,
            rv_editor_log_channel::out,
            proc_.pid(),
            number_);
        out_partial_.clear();
    }
}

int rv_editor_session::pause(rv_editor_log &log, std::string &error)
{
    const bool can_pause = state_ == rv_editor_run_state::running && proc_.running();
    if (!can_pause) {
        error = "the runtime is not running";
        return RV_ERR_BUSY;
    }
    int64_t id = 0;
    if (send(std::string(cmd_pause), log, id, error) != RV_OK) {
        return RV_ERR_IO;
    }
    state_ = rv_editor_run_state::pausing;
    return RV_OK;
}

int rv_editor_session::resume(rv_editor_log &log, std::string &error)
{
    const bool can_resume = state_ == rv_editor_run_state::paused && proc_.running();
    if (!can_resume) {
        error = "the runtime is not paused";
        return RV_ERR_BUSY;
    }
    int64_t id = 0;
    if (send(std::string(cmd_resume), log, id, error) != RV_OK) {
        return RV_ERR_IO;
    }
    state_ = rv_editor_run_state::resuming;
    return RV_OK;
}

int rv_editor_session::step(rv_editor_log &log, std::string &error)
{
    const bool can_step = state_ == rv_editor_run_state::paused && proc_.running();
    if (!can_step) {
        error = "the runtime is not paused";
        return RV_ERR_BUSY;
    }
    int64_t id = 0;
    if (send(std::string(cmd_step), log, id, error) != RV_OK) {
        return RV_ERR_IO;
    }
    state_ = rv_editor_run_state::stepping;
    return RV_OK;
}

int rv_editor_session::stop(rv_editor_log &log, std::string &error)
{
    if (!proc_.running()) {
        error = "runtime already stopped";
        return RV_ERR_ALREADY;
    }
    if (quit_sent_) {
        error = "stop already requested; Force Stop is offered if the runtime does not end";
        return RV_ERR_ALREADY;
    }
    quit_sent_ = true;
    stop_sent_ = std::chrono::steady_clock::now();
    state_ = rv_editor_run_state::stopping;
    int64_t quit_id = 0;
    if (send(std::string(cmd_quit), log, quit_id, error) == RV_OK) {
        return RV_OK;
    }
    if (proc_.stdin_open()) {
        return RV_ERR_BUSY;
    }
    // Nobody reads the channel any more: the process can only be ended. One
    // that only stopped reading shows as hung and gets Force Stop.
    std::string force_error;
    const int force_result = force_stop(log, force_error);
    if (force_result != RV_OK) {
        error += "; " + force_error;
    }
    return force_result;
}

int rv_editor_session::force_stop(rv_editor_log &log, std::string &error)
{
    if (!proc_.running()) {
        error = "runtime already stopped";
        return RV_ERR_ALREADY;
    }
    forced_ = true;
    log.add(rv_editor_log_source::editor,
        rv_editor_log_level::warning,
        "force-stopping runtime pid " + std::to_string(proc_.pid()),
        rv_editor_log_channel::none,
        proc_.pid(),
        number_);
    const int stop_result = proc_.stop(true);
    if (stop_result != RV_OK) {
        error = "pid " + std::to_string(proc_.pid()) + ": the signal was not delivered";
        return stop_result;
    }
    return RV_OK;
}

void rv_editor_session::force_stop_recorded(rv_editor_log &log)
{
    std::string error;
    const int result = force_stop(log, error);
    if (result != RV_OK) {
        record_follow_up(result, "cannot force-stop runtime: " + error);
    }
}

void rv_editor_session::record_follow_up(int code, const std::string &text)
{
    // Keeps the first refusal recorded since update() last returned one; later ones add their text.
    if (follow_up_code_ == RV_OK) {
        follow_up_code_ = code;
    }
    // A read that keeps failing repeats the same refusal every frame: a text already there is not added again.
    if (follow_up_error_.find(text) == std::string::npos) {
        rv_editor_append_failure(follow_up_error_, text);
    }
}

int rv_editor_session::take_follow_up(bool read_failed, std::string &error)
{
    // While a read failed and the session lives, a later update() returns it; otherwise nothing else would.
    if (follow_up_code_ == RV_OK || (read_failed && live())) {
        return RV_OK;
    }
    const int code = follow_up_code_;
    error = std::move(follow_up_error_);
    follow_up_code_ = RV_OK;
    follow_up_error_.clear();
    return code;
}

bool rv_editor_session::pad_pending(uint64_t buttons) const
{
    return handshake_done_ && proc_.running() && !quit_sent_ && buttons != pad_sent_;
}

int rv_editor_session::pad(uint64_t buttons, rv_editor_log &log, std::string &error)
{
    const bool can_pad = handshake_done_ && proc_.running() && !quit_sent_;
    if (!can_pad) {
        error = "the runtime is not ready for requests";
        return RV_ERR_BUSY;
    }
    if (buttons == pad_sent_) {
        error = "the pad state is already sent";
        return RV_ERR_ALREADY;
    }
    char hex[hex_buffer_size];
    std::snprintf(hex, sizeof(hex), "%llx", static_cast<unsigned long long>(buttons));
    const std::string verb = std::string(pad_prefix) + hex;
    int64_t id = 0;
    if (send(verb, log, id, error) != RV_OK) {
        return RV_ERR_IO;
    }
    pad_sent_ = buttons;
    return RV_OK;
}

int rv_editor_session::update(rv_editor_log &log, std::string &error)
{
    // A live session keeps reading every frame, exited console or not: finish()
    // runs once, only when the console's output is complete (output_done()).
    if (!live()) {
        return RV_OK;
    }

    std::string out;
    std::string err;
    const int read_result = proc_.read(out, err, read_buffer_size, error);
    proc_.flush();
    log.add_stream(rv_editor_log_source::runtime, err_partial_, err, rv_editor_log_channel::err, proc_.pid(), number_);
    if (!out.empty()) {
        trace(out, log);
        std::vector<rv_editor_devmsg> msgs;
        std::vector<std::string> errors;
        parser_.feed(out, msgs, errors);
        for (const std::string &e : errors) {
            log.add(rv_editor_log_source::editor,
                rv_editor_log_level::warning,
                "protocol: " + e,
                rv_editor_log_channel::none,
                proc_.pid(),
                number_);
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
        if (channel_open_ && eof_at_ != std::chrono::steady_clock::time_point{} && now - eof_at_ > channel_lost_timeout) {
            channel_open_ = false;
            if (!quit_sent_) {
                state_ = rv_editor_run_state::disconnected;
                end_reason_ = "the protocol channel closed while the process still runs";
                log.add(rv_editor_log_source::editor,
                    rv_editor_log_level::error,
                    end_reason_,
                    rv_editor_log_channel::none,
                    proc_.pid(),
                    number_);
            }
        }

        // A timeout proves nothing about whether the request ran: say so,
        // ask for status, and never resend the request itself.
        bool ask_status = false;
        for (auto &[id, req] : pending_) {
            const auto limit =
                !handshake_done_ && req.verb == "status" ? rv_editor_handshake_timeout : rv_editor_request_timeout;
            if (req.overdue || now - req.sent < limit) {
                continue;
            }
            req.overdue = true;
            if (!handshake_done_) {
                state_ = rv_editor_run_state::refused;
                refusal_ = "no answer to status: not a development console, or it could not load the disc";
                end_reason_ = refusal_;
                log.add(rv_editor_log_source::editor,
                    rv_editor_log_level::error,
                    refusal_,
                    rv_editor_log_channel::none,
                    proc_.pid(),
                    number_);
                force_stop_recorded(log);
                break;
            }
            uncertain_ = true;
            // Whether it ran is unknown; the user may ask again, the editor never does.
            note_reload_timeout(id, req.verb);
            ask_status = ask_status || (req.verb != "status" && req.verb != "quit");
            log.add(rv_editor_log_source::editor,
                rv_editor_log_level::warning,
                "no answer to '" + req.verb + "' after " + std::to_string(rv_editor_request_timeout.count()) +
                    " s: whether it ran is unknown",
                rv_editor_log_channel::none,
                proc_.pid(),
                number_);
        }
        if (ask_status && channel_open_) {
            int64_t status_id = 0;
            std::string status_error;
            if (send(std::string(cmd_status), log, status_id, status_error) != RV_OK) {
                note_send_failure(cmd_status, status_error);
            }
        }

        if (state_ == rv_editor_run_state::refused && hung()) {
            force_stop_recorded(log);
        }
    }

    // finish() runs whatever the read gave.
    std::string finish_error;
    int finish_result = RV_OK;
    if (proc_.poll() && proc_.output_done()) {
        finish_result = finish(log, finish_error);
    }
    std::string follow_up_error;
    const int follow_up_result = take_follow_up(read_result != RV_OK, follow_up_error);
    // The code is the first failure's; the error text lists all of them in the same order.
    const int code = rv_editor_first_failure({ read_result, follow_up_result, finish_result });
    if (code != RV_OK) {
        std::string text;
        if (read_result != RV_OK) {
            rv_editor_append_failure(text, "output not read: " + error);
        }
        if (follow_up_result != RV_OK) {
            rv_editor_append_failure(text, follow_up_error);
        }
        if (finish_result != RV_OK) {
            rv_editor_append_failure(text, finish_error);
        }
        error = text;
        return code;
    }
    return RV_OK;
}

int rv_editor_session::finish(rv_editor_log &log, std::string &error)
{
    std::string out;
    std::string err;
    const int read_result = proc_.read(out, err, read_buffer_size, error);
    log.add_stream(rv_editor_log_source::runtime, err_partial_, err, rv_editor_log_channel::err, proc_.pid(), number_);
    log.flush_stream(rv_editor_log_source::runtime, err_partial_, rv_editor_log_channel::err, proc_.pid(), number_);
    trace(out, log);
    log.flush_stream(rv_editor_log_source::protocol, out_partial_, rv_editor_log_channel::out, proc_.pid(), number_);
    if (proc_.output_cut()) {
        log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            "runtime output after exit was not fully read: a process it started kept a pipe open past the grace",
            rv_editor_log_channel::none,
            proc_.pid(),
            number_);
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
        end_reason_ = std::string(end_reason_force_stopped) + " (" + how + ")";
    } else if (exit.signal != 0 || exit.code != 0) {
        state_ = rv_editor_run_state::crashed;
        end_reason_ = how;
    } else {
        state_ = rv_editor_run_state::exited;
        end_reason_ = quit_sent_ ? "stopped" : "the console ended by itself";
    }
    log.add(rv_editor_log_source::editor,
        state_ == rv_editor_run_state::crashed || state_ == rv_editor_run_state::refused ? rv_editor_log_level::error :
                                                                                           rv_editor_log_level::info,
        "runtime pid " + std::to_string(proc_.pid()) + " ended: " + end_reason_,
        rv_editor_log_channel::none,
        proc_.pid(),
        number_);
    return read_result;
}

} // namespace rv_editor
