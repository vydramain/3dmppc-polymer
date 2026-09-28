#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "log/rv_editor_log.hpp"
#include "pdklib/rv_version/rv_version.hpp"
#include "platform/rv_editor_process.hpp"
#include "platform/rv_editor_shm.hpp"
#include "session/rv_editor_devproto.hpp"

namespace rv_editor
{

// What the runtime is doing, as far as the console has confirmed it (section 12
// of the requirements). Pending states last until the console answers.
enum class rv_editor_run_state
{
    stopped,      // never started in this window
    starting,     // process up, waiting for the first status
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
struct rv_editor_session_facts
{
    std::string disc;
    std::string code_hash;  // of the disc.so it mapped
    std::string pdk;
    std::string medium;     // "live": a directory; "fixed": an image that cannot change
    bool reloadable = false; // the entry script can be reloaded
    int64_t revision = -1;   // the entry's; -1 before the first status
    int64_t first_revision = -1; // at connect: a later one came by reload
    std::string entry_hash;
    int64_t lua_used = 0;
    int64_t lua_budget = 0;  // 0: the disc declares no Lua machine
    std::chrono::system_clock::time_point at{};
};

// One `get` or `keys` answer. Answers are separate reads: `frame_exact` says the
// machine was paused when it was asked, so it describes `frame`; otherwise it is
// a sample of a running machine taken at `at`.
struct rv_editor_answer
{
    bool ok = false;
    std::vector<std::pair<std::string, std::string>> fields;
    std::string error;
    bool frame_exact = false;
    int64_t frame = 0;
    std::chrono::system_clock::time_point at{};
};

// One runtime session (DEV-01..DEV-10): `3dmppc --dev` as a child process with
// its own window, its stdin/stdout the protocol and its stderr the log. The
// session lives outside the UI; closing Game, Output or Controls changes nothing
// here (LAY-09). One per window (PRJ-09).
class rv_editor_session
{
public:
    // Protocol this client speaks: the PDK version, exact match with the console's.
    static constexpr const char *protocol_supported = rv_pdklib::rv_version_str;

    // False with the reason when a session cannot start now.
    // `options` go before the disc, `env` over the editor's environment (a run profile).
    bool start(const std::filesystem::path &console, const std::filesystem::path &disc_dir,
        const std::filesystem::path &memcard, const std::filesystem::path &cwd, uint32_t build_number,
        const std::vector<std::string> &options, const std::vector<std::string> &env, rv_editor_log &log,
        std::string &error);

    void pause(rv_editor_log &log);
    void resume(rv_editor_log &log);
    void step(rv_editor_log &log);
    // `quit`, then waits for the process to end.
    void stop(rv_editor_log &log);
    // SIGKILL to a process this session owns, for one that does not end.
    void force_stop(rv_editor_log &log);
    // Asks `request` ("get a b", "keys a"); the answer lands in answers() under the
    // same text. 0 when nothing was sent.
    int64_t query(const std::string &request, rv_editor_log &log);
    // `reload entry`: the entry script again from the directory the disc runs from.
    void reload(rv_editor_log &log);
    // A fresh status, for the facts.
    void refresh(rv_editor_log &log);
    // `pad 0 <hex>` when `buttons` (rv_isource bits) differ from the last sent:
    // what the Game tile's keyboard holds.
    void pad(uint64_t buttons, rv_editor_log &log);

    // The frames the console writes (--frame-fd); the Game tile reads them.
    rv_editor_frame_memory &frame_memory() { return frame_mem_; }

    // Once a frame: reads the channel and the log, notices timeouts and the end.
    void update(rv_editor_log &log);

    // Ends the session for good before the editor exits: quit, a short wait,
    // then kill. Leaves no orphan (DEV-09).
    void shutdown(rv_editor_log &log);

    rv_editor_run_state state() const { return state_; }
    bool live() const;               // the process is running
    bool hung() const;               // stopping for longer than it should
    bool uncertain() const { return uncertain_; } // a request timed out: its effect is unknown
    int64_t frame() const { return frame_; }
    pid_t pid() const { return proc_.pid(); }
    uint32_t build_number() const { return build_number_; }
    const std::string &end_reason() const { return end_reason_; }
    const std::filesystem::path &disc_dir() const { return disc_dir_; }
    bool connected() const { return handshake_done_ && live(); }
    bool reloading() const { return reloading_; }
    // The last reload's answer in this session, as the runtime gave it; empty before one.
    const std::string &reload_result() const { return reload_result_; }
    bool reload_ok() const { return reload_ok_; }
    // Counts the sessions this window started, from 1; 0 before the first.
    uint32_t number() const { return number_; }
    std::chrono::system_clock::time_point started_at() const { return started_wall_; }
    // When the process ended; the epoch while it runs.
    std::chrono::system_clock::time_point ended_at() const { return ended_wall_; }
    const rv_editor_session_facts &facts() const { return facts_; }
    const std::map<std::string, rv_editor_answer> &answers() const { return answers_; }

private:
    struct rv_editor_request
    {
        std::string verb;
        std::chrono::steady_clock::time_point sent;
        bool overdue = false; // timed out and reported; a late answer still counts
        bool paused = false;  // the machine was paused when it was sent
        int64_t frame = 0;    // the frame reported last when it was sent
    };

    int64_t send(const std::string &verb, rv_editor_log &log);
    void note_facts(const rv_editor_devmsg &msg);
    // A `get`, `keys` or `reload entry` answer; false for any other request.
    bool handle_query(const rv_editor_request &req, const rv_editor_devmsg &msg, rv_editor_log &log);
    // The protocol trace from stdout bytes, without the frame events.
    void trace(std::string_view bytes, rv_editor_log &log);
    void handle(const rv_editor_devmsg &msg, rv_editor_log &log);
    void handle_mode(std::string_view mode);
    void finish(rv_editor_log &log);

    rv_editor_process proc_;
    rv_editor_frame_memory frame_mem_;
    uint64_t pad_sent_ = 0;
    rv_editor_devparser parser_;
    rv_editor_run_state state_ = rv_editor_run_state::stopped;
    std::map<int64_t, rv_editor_request> pending_;
    int64_t next_id_ = 1;
    int64_t frame_ = 0;
    bool uncertain_ = false;
    bool quit_sent_ = false;
    bool forced_ = false;
    bool handshake_done_ = false;
    bool channel_open_ = false;
    bool input_full_ = false; // said once: the runtime stopped reading its input
    uint32_t build_number_ = 0;
    uint32_t number_ = 0;
    bool reloading_ = false;
    std::string reload_result_;
    bool reload_ok_ = false;
    rv_editor_session_facts facts_;
    std::map<std::string, rv_editor_answer> answers_;
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
