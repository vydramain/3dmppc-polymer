#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include "log/rv_editor_log.hpp"
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

// One runtime session (DEV-01..DEV-10): `3dmppc --dev` as a child process with
// its own window, its stdin/stdout the protocol and its stderr the log. The
// session lives outside the UI; closing Game, Output or Controls changes nothing
// here (LAY-09). One per window (PRJ-09).
class rv_editor_session
{
public:
    // Protocol versions this client speaks (docs/adr/0008-protocol-version.md).
    static constexpr int protocol_supported = 2;

    // False with the reason when a session cannot start now.
    bool start(const std::filesystem::path &console, const std::filesystem::path &disc_dir,
        const std::filesystem::path &memcard, const std::filesystem::path &cwd, uint32_t build_number,
        rv_editor_log &log, std::string &error);

    void pause(rv_editor_log &log);
    void resume(rv_editor_log &log);
    void step(rv_editor_log &log);
    // `quit`, then waits for the process to end.
    void stop(rv_editor_log &log);
    // SIGKILL to a process this session owns, for one that does not end.
    void force_stop(rv_editor_log &log);
    // `pad 0 <hex>` when `buttons` (rv_isource bits) differ from the last sent:
    // what the Game tile's keyboard holds (docs/adr/0006-game-frame.md).
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

private:
    int64_t send(const std::string &verb, rv_editor_log &log);
    void handle(const rv_editor_devmsg &msg, rv_editor_log &log);
    void handle_mode(std::string_view mode);
    void finish(rv_editor_log &log);

    struct rv_editor_request
    {
        std::string verb;
        std::chrono::steady_clock::time_point sent;
        bool overdue = false; // timed out and reported; a late answer still counts
    };

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
    uint32_t build_number_ = 0;
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
