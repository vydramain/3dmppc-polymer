#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "log/rv_editor_log.hpp"
#include "pdk/rv_err.h"
#include "pdklib/rv_version/rv_version.hpp"
#include "platform/rv_editor_process.hpp"
#include "platform/rv_editor_shm.hpp"
#include "session/rv_editor_devproto.hpp"

namespace rv_editor
{

// Prefix of session end reason after a forced stop; the release flow checks for it.
inline constexpr std::string_view end_reason_force_stopped = "force-stopped";

// What the runtime is doing, as far as the console has confirmed it (section 12
// of the requirements). Pending states last until the console answers.
enum class rv_editor_run_state {
    stopped,  // never started in this window
    starting, // process up, waiting for the first status
    running,
    pausing,
    paused,
    stepping,
    resuming,
    stopping,     // quit sent, waiting for the process to end
    exited,       // ended after quit, or by its own window closing
    crashed,      // ended by a signal or a non-zero exit code
    disconnected, // the channel closed under a live process
    refused,      // not a development console this editor speaks to
};

const char *rv_editor_run_state_name(rv_editor_run_state state);

// What the console said about itself in its last status (README.md, "The
// channel"): the disc it loaded, which code, and the Lua half's revision.
struct rv_editor_session_facts {
    std::string disc;
    std::string code_hash; // of the disc.so it mapped
    std::string pdk;
    std::string medium;          // "live": a directory; "fixed": an image that cannot change
    bool reloadable = false;     // the entry script can be reloaded
    int64_t revision = -1;       // the entry's; -1 before the first status
    int64_t first_revision = -1; // at connect: a later one came by reload
    std::string entry_hash;
    int64_t lua_used = 0;
    int64_t lua_budget = 0; // 0: the disc declares no Lua machine
    std::chrono::system_clock::time_point at{};
};

// One `get` or `keys` answer. Answers are separate reads: `frame_exact` says the
// machine was paused when it was asked, so it describes `frame`; otherwise it is
// a sample of a running machine taken at `at`.
struct rv_editor_answer {
    bool ok = false;
    std::vector<std::pair<std::string, std::string>> fields;
    std::string error;
    bool frame_exact = false;
    int64_t frame = 0;
    std::chrono::system_clock::time_point at{};
};

// The last `ok` reply this session handled, for an end-of-session summary: what
// it answered, the frame after it was applied, when. Empty verb before any.
struct rv_editor_last_confirmed {
    std::string verb;
    int64_t frame = 0;
    std::chrono::system_clock::time_point at{};
};

// One request still pending when finish() ran, before it clears them.
struct rv_editor_in_flight {
    std::string verb;
    bool overdue = false;
};

// What the last reload-slot result is about: the entry, a required module, or
// a texture sent by reload_asset().
enum class rv_editor_reload_kind {
    entry,
    module,
    texture,
};

// What start() runs: the console, its disc and card, the working directory, the
// build number, `options` (before the disc) and `env` (over the editor's environment).
struct rv_editor_session_launch {
    std::filesystem::path console;
    std::filesystem::path disc_dir;
    std::filesystem::path memcard;
    std::filesystem::path cwd;
    uint32_t build_number = 0;
    std::vector<std::string> options;
    std::vector<std::string> env;
};

// One runtime session: `3dmppc --dev` as a child process with
// its own window, its stdin/stdout the protocol and its stderr the log. The
// session lives outside the UI; closing Game, Output or Controls changes nothing
// here. One per window.
class rv_editor_session
{
public:
    // Protocol this client speaks: the PDK version, exact match with the console's.
    static constexpr const char *protocol_supported = rv_pdklib::rv_version_str;
    // The console's own ceiling for a request's payload (rv_pccmdchan.hpp,
    // RV_PCCMDCHAN_PAYLOAD_MAX): sending more is refused there before it is tried.
    static constexpr size_t asset_payload_max = 4 << 20;

    // RV_OK on success; RV_ERR_* on failure with the reason in error. Writes the
    // "runtime started" line; a failed handshake also logs from force_stop().
    // After RV_ERR_IO live() stays true until finish() reaps the killed process; a
    // start before that is RV_ERR_BUSY.
    int start(const rv_editor_session_launch &launch, rv_editor_log &log, std::string &error);

    // RV_OK on success; RV_ERR_BUSY if the runtime is not in the right state, RV_ERR_IO if send fails.
    // The reason of a refusal is in error; the caller logs it.
    int pause(rv_editor_log &log, std::string &error);
    int resume(rv_editor_log &log, std::string &error);
    int step(rv_editor_log &log, std::string &error);
    // `quit`, then waits for the process to end. RV_OK on success; RV_ERR_ALREADY when the
    // runtime is gone or a stop was already requested; RV_ERR_BUSY if send blocked and stdin
    // open. A refusal's reason is in error. A quit not delivered before a force-stop is logged
    // with the force-stop event.
    int stop(rv_editor_log &log, std::string &error);
    // SIGKILL to a process this session owns, for one that does not end. RV_ERR_ALREADY when
    // the runtime is gone; otherwise the failure's code with the reason in error.
    int force_stop(rv_editor_log &log, std::string &error);
    // Asks `request` ("get a b", "keys a"); the answer lands in answers() under the
    // same text. RV_OK on success; RV_ERR_BUSY when the runtime is not ready for
    // requests, RV_ERR_IO when send fails. The reason is in error; nothing is logged.
    int query(const std::string &request, rv_editor_log &log, std::string &error);
    // Empty `module`: `reload entry`, the entry script again. Otherwise `reload module
    // <module>`, that required module again, both off the drive the disc runs from.
    // RV_OK on success; RV_ERR_BUSY when the runtime is not ready or a reload is in flight,
    // RV_ERR_IO when send fails. The reason is in error; nothing is logged.
    int reload(rv_editor_log &log, std::string &error, const std::string &module = std::string());
    // `asset <name> bytes <n>`: sends a baked texture's bytes in one process write,
    // sharing reload()'s in-flight slot. RV_OK on success; RV_ERR_BUSY when the runtime is not
    // ready or a reload is in flight; RV_ERR_INVAL for bytes over the console's payload ceiling or
    // over rv_editor_process::input_max once the header is added; RV_ERR_IO when send fails.
    // The reason is in error; nothing is logged.
    int reload_asset(rv_editor_log &log, const std::string &name, const std::vector<unsigned char> &bytes, std::string &error);
    // A fresh status, for the facts. RV_OK on success; RV_ERR_BUSY when the runtime is
    // not ready for requests, RV_ERR_IO when send fails. The reason is in error; the caller logs it.
    int refresh(rv_editor_log &log, std::string &error);
    // `pad 0 <hex>` when `buttons` (rv_isource bits) differ from the last sent:
    // what the Game tile's keyboard holds.
    void pad(uint64_t buttons, rv_editor_log &log);

    // The frames the console writes (--frame-fd); the Game tile reads them.
    rv_editor_frame_memory &frame_memory()
    {
        return frame_mem_;
    }

    // Once a frame: reads the channel and the log, notices timeouts and the end.
    // RV_ERR_IO: a read failed, the reason is in `error`; the rest of the frame still runs.
    int update(rv_editor_log &log, std::string &error);

    // Ends the session for good before the editor exits: quit, a short wait,
    // then kill. Leaves no orphan.
    // RV_ERR_IO: a read failed during the end; the first reason is in `error`.
    int shutdown(rv_editor_log &log, std::string &error);

    rv_editor_run_state state() const
    {
        return state_;
    }
    // True from a successful start() until finish() has run once: stays true while
    // an exited console's output is still being read (see finish()).
    bool live() const;
    bool hung() const; // stopping for longer than it should
    bool uncertain() const
    {
        return uncertain_;
    } // a request timed out: its effect is unknown
    int64_t frame() const
    {
        return frame_;
    }
    pid_t pid() const
    {
        return proc_.pid();
    }
    uint32_t build_number() const
    {
        return build_number_;
    }
    const std::string &end_reason() const
    {
        return end_reason_;
    }
    const std::filesystem::path &disc_dir() const
    {
        return disc_dir_;
    }
    bool connected() const
    {
        return handshake_done_ && proc_.running();
    }
    bool reloading() const
    {
        return reloading_;
    }
    // The last reload's answer in this session, as the runtime gave it; empty before one.
    const std::string &reload_result() const
    {
        return reload_result_;
    }
    bool reload_ok() const
    {
        return reload_ok_;
    }
    // What that last result is about: empty for the entry, else a module or texture name.
    const std::string &reload_target() const
    {
        return reload_target_;
    }
    // entry/module/texture: which kind reload_target() names.
    rv_editor_reload_kind reload_kind() const
    {
        return reload_kind_;
    }
    // Counts the sessions this window started, from 1; 0 before the first.
    uint32_t number() const
    {
        return number_;
    }
    std::chrono::system_clock::time_point started_at() const
    {
        return started_wall_;
    }
    // When the process ended; the epoch while it runs.
    std::chrono::system_clock::time_point ended_at() const
    {
        return ended_wall_;
    }
    const rv_editor_session_facts &facts() const
    {
        return facts_;
    }
    const std::map<std::string, rv_editor_answer> &answers() const
    {
        return answers_;
    }
    // Resource names of every ".scene.toml" the running disc has opened this session.
    const std::set<std::string> &scenes_read() const
    {
        return scenes_read_;
    }
    // For an end-of-session summary: the last confirmed reply, and every request
    // still pending when finish() ran.
    const rv_editor_last_confirmed &last_confirmed() const
    {
        return last_confirmed_;
    }
    const std::vector<rv_editor_in_flight> &in_flight() const
    {
        return in_flight_;
    }
    const rv_editor_process::rv_editor_exit &exit_status() const
    {
        return proc_.exit_status();
    }
    bool output_cut() const
    {
        return proc_.output_cut();
    }
    const std::filesystem::path &console() const
    {
        return console_;
    }

private:
    struct rv_editor_request {
        std::string verb;
        std::chrono::steady_clock::time_point sent;
        bool overdue = false; // timed out and reported; a late answer still counts
        bool paused = false;  // the machine was paused when it was sent
        int64_t frame = 0;    // the frame reported last when it was sent
    };

    // RV_OK and the request's number in id; RV_ERR_IO and the reason in error, nothing logged.
    // `payload`, when not empty, follows the header line in the same process write
    // (README.md, "A request carries bytes..."); the log line shows the header only.
    int send(const std::string &verb,
        rv_editor_log &log,
        int64_t &id,
        std::string &error,
        std::string_view payload = std::string_view());
    // Logs a refused send once per run of refusals: a closed input every time, a full one on its first refusal.
    void log_send_failure(std::string_view verb, const std::string &error, rv_editor_log &log);
    // force_stop with no caller to return the reason to: the reason is logged as an error.
    void force_stop_logged(rv_editor_log &log);
    void note_facts(const rv_editor_devmsg &msg);
    // A `get`, `keys` or `reload` answer; false for any other request.
    bool handle_query(const rv_editor_request &req, const rv_editor_devmsg &msg, rv_editor_log &log);
    // A pending request `id` (verb `verb`) timed out; if it is the reload in flight,
    // clears reloading_ and sets an "unknown" result for it.
    void note_reload_timeout(int64_t id, const std::string &verb);
    // The protocol trace from stdout bytes, without the frame events.
    void trace(std::string_view bytes, rv_editor_log &log);
    void handle(const rv_editor_devmsg &msg, rv_editor_log &log);
    void handle_mode(std::string_view mode);
    // Returns the result of its read; the end is recorded either way.
    int finish(rv_editor_log &log, std::string &error);
    // The steps of start(): spawn and handshake give the reason in error; reset_state cannot fail.
    int spawn(const std::vector<std::string> &argv, const rv_editor_session_launch &launch, std::string &error);
    void reset_state(const rv_editor_session_launch &launch);
    int handshake(rv_editor_log &log, std::string &error);

    rv_editor_process proc_;
    rv_editor_frame_memory frame_mem_;
    uint64_t pad_sent_ = 0;
    rv_editor_devparser parser_;
    rv_editor_run_state state_ = rv_editor_run_state::stopped;
    bool active_ = false; // start() succeeded and finish() has not run yet (live())
    std::map<int64_t, rv_editor_request> pending_;
    int64_t next_id_ = 1;
    int64_t frame_ = 0;
    bool uncertain_ = false;
    bool quit_sent_ = false;
    bool forced_ = false;
    bool handshake_done_ = false;
    bool channel_open_ = false;
    bool input_full_ = false; // the runtime stopped reading its input: set by a refused open-input send, cleared by a sent one
    bool send_failure_reported_ = false; // the current run of refusals to a full input is logged; cleared with input_full_
    uint32_t build_number_ = 0;
    uint32_t number_ = 0;
    bool reloading_ = false;
    int64_t reload_id_ = 0; // request id of the reload in flight/last shown; a late answer for another id is ignored
    std::string reload_result_;
    bool reload_ok_ = false;
    std::string reload_target_; // empty: the entry; else the module/texture the last result is about
    rv_editor_reload_kind reload_kind_ = rv_editor_reload_kind::entry;
    rv_editor_session_facts facts_;
    std::map<std::string, rv_editor_answer> answers_;
    std::set<std::string> scenes_read_;
    rv_editor_last_confirmed last_confirmed_;
    std::vector<rv_editor_in_flight> in_flight_;
    std::filesystem::path console_;
    std::chrono::system_clock::time_point started_wall_{};
    std::chrono::system_clock::time_point ended_wall_{};
    std::filesystem::path disc_dir_;
    std::string end_reason_;
    std::string refusal_; // why this editor refused the console, before it ended
    std::string err_partial_;
    std::string out_partial_; // the protocol trace, line by line
    std::chrono::steady_clock::time_point started_{};
    std::chrono::steady_clock::time_point stop_sent_{};
    std::chrono::steady_clock::time_point eof_at_{};
};

} // namespace rv_editor
