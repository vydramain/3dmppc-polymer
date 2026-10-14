// The player: a 3dmppc built without devtools plays a candidate's image in its
// own window. The editor sees its PID, its output and how it ended, and
// nothing else: no pause, step or reload reaches it.

#include "app/rv_editor_app.hpp"

#include <chrono>
#include <string>

#include "project/rv_editor_toml.hpp"
#include "release/rv_editor_candidate_store.hpp"
#include "text/rv_editor_text.hpp"

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Bytes read from the player per poll.
constexpr size_t read_chunk_bytes = 65536;
// Cache subdirectory holding release candidates and their logs.
constexpr std::string_view candidates_dir_name = "candidates";
// Prefix of a player run's log name within the candidates directory.
constexpr std::string_view player_log_prefix = "player-";
// Extension of the player's own memory card, separate from development saves.
constexpr std::string_view player_card_extension = ".player.mppccard";
// Command-line flag for the player's memory card path.
constexpr std::string_view memcard_flag = "--memcard";

// The candidate's number the player runs, or 0 outside a run.
uint32_t rv_editor_player_run(const rv_editor_release &r)
{
    return r.player_candidate >= 0 && static_cast<size_t>(r.player_candidate) < r.candidates.size() ?
        r.candidates[static_cast<size_t>(r.player_candidate)].number :
        0;
}

// One stream's output as log lines, each whole; a line still being written waits.
void rv_editor_player_lines(rv_editor_app &app, rv_editor_log_channel channel, std::string &partial, const std::string &bytes)
{
    rv_editor_release &r = app.release;
    partial += bytes;
    const int64_t pid = r.player->pid();
    const uint32_t run = rv_editor_player_run(r);
    size_t nl = 0;
    while ((nl = partial.find('\n')) != std::string::npos) {
        const std::string line = partial.substr(0, nl);
        partial.erase(0, nl + 1);
        app.log.add(rv_editor_log_source::runtime, rv_editor_log_level::info, "player: " + line, channel, pid, run);
        r.player_output += line + "\n";
    }
}

// The first free player-<k>.log of a candidate. RV_ERR_IO with the reason in error when a name cannot be checked.
int rv_editor_player_log_path(const std::filesystem::path &dir, uint32_t number, std::filesystem::path &log, std::string &error)
{
    std::error_code ec;
    uint32_t k = 1;
    log = rv_editor_candidate_log(dir, number, std::string(player_log_prefix) + std::to_string(k));
    while (std::filesystem::exists(log, ec)) {
        ++k;
        log = rv_editor_candidate_log(dir, number, std::string(player_log_prefix) + std::to_string(k));
    }
    if (ec) {
        error = "cannot look for a free name: " + ec.message();
        return RV_ERR_IO;
    }
    return RV_OK;
}

// Keeps the player's output beside its candidate's record. RV_OK, or RV_ERR_IO with the reason logged.
int rv_editor_player_keep_log(rv_editor_app &app, const rv_editor_candidate &c)
{
    const rv_editor_release &r = app.release;
    const std::filesystem::path dir = app.project.cache_dir / std::string(candidates_dir_name);
    std::filesystem::path log;
    std::string error;
    const int named = rv_editor_player_log_path(dir, c.number, log, error);
    const int kept = named != RV_OK ? named : rv_editor_file_replace(log, r.player_output, error);
    if (kept != RV_OK) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "player log not kept: " + error,
            rv_editor_log_channel::none,
            r.player->pid(),
            rv_editor_player_run(r));
    }
    return kept;
}

// Records the player's end on its candidate, then keeps its output. RV_OK, or RV_ERR_IO (logged).
int rv_editor_player_record(rv_editor_app &app, const rv_editor_process::rv_editor_exit &exit, const std::string &ended)
{
    rv_editor_release &r = app.release;
    rv_editor_candidate &c = r.candidates[static_cast<size_t>(r.player_candidate)];
    // Stopped by the editor is the operator's act and proves no normal exit.
    const rv_editor_check_state state = r.player_stopped ? rv_editor_check_state::not_run :
        exit.signal == 0 && exit.code == 0               ? rv_editor_check_state::passed :
                                                           rv_editor_check_state::failed;
    const std::string note = r.player_stopped  ? "stopped by the operator (" + ended + "): no normal exit shown" :
        state == rv_editor_check_state::passed ? "ended by itself, " + ended :
                                                 "ended " + ended;
    rv_editor_check_set(c, rv_editor_check_player, state, note, app.tools.player.path.string());
    return rv_editor_player_keep_log(app, c);
}

} // namespace

int rv_editor_app_player_finish(rv_editor_app &app)
{
    rv_editor_release &r = app.release;
    if (r.player == nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "player not finished: no player");
        return RV_ERR_INVAL;
    }
    const int64_t pid = r.player->pid();
    const uint32_t run = rv_editor_player_run(r);
    rv_editor_player_lines(app,
        rv_editor_log_channel::out,
        r.player_out_partial,
        r.player_out_partial.empty() ? std::string() : "\n");
    rv_editor_player_lines(app,
        rv_editor_log_channel::err,
        r.player_err_partial,
        r.player_err_partial.empty() ? std::string() : "\n");
    if (r.player->output_cut()) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            "player output after exit was not fully read: a process it started kept a pipe open past the grace",
            rv_editor_log_channel::none,
            pid,
            run);
    }
    const rv_editor_process::rv_editor_exit exit = r.player->exit_status();
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - r.player_started).count();
    const std::string ended = rv_editor_exit_text(exit) + " after " + std::to_string(seconds) + " s";
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::info,
        "player ended: " + ended,
        rv_editor_log_channel::none,
        pid,
        run);
    const bool has_candidate = r.player_candidate >= 0 && static_cast<size_t>(r.player_candidate) < r.candidates.size();
    const int kept = has_candidate ? rv_editor_player_record(app, exit, ended) : RV_OK;
    r.player.reset();
    r.player_candidate = -1;
    return kept;
}

const char *rv_editor_app_why_not_play(const rv_editor_app &app)
{
    const rv_editor_release &r = app.release;
    if (r.candidates.empty()) {
        return rv_editor_text("app_player.no_candidate");
    }
    if (r.player != nullptr) {
        return rv_editor_text("app_player.player_running");
    }
    const rv_editor_candidate &c = r.candidates[r.selected];
    if (c.bytes_changed) {
        return rv_editor_text("app_player.image_bytes_changed");
    }
    if (c.sha256.empty()) {
        return rv_editor_text("app_player.hashing_image");
    }
    if (!app.tools.player.problem.empty()) {
        return app.tools.player.problem.c_str();
    }
    return nullptr;
}

int rv_editor_app_play_candidate(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_play(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not run: ") + why);
        return RV_ERR_BUSY;
    }
    rv_editor_release &r = app.release;
    rv_editor_candidate &c = r.candidates[r.selected];
    // Its identity is read again while it plays; a difference voids the results.
    rv_editor_candidate_hash(c);
    // A card of its own: neither development saves nor the dev playtest's reach it.
    std::filesystem::path card = c.image;
    card.replace_extension(std::string(player_card_extension));
    auto player = std::make_unique<rv_editor_process>();
    std::string error;
    const int err =
        player->start({ app.tools.player.path.string(), std::string(memcard_flag), card.string(), c.image.string() },
            c.image.parent_path(),
            error);
    if (err != RV_OK) {
        rv_editor_check_set(c,
            rv_editor_check_player,
            rv_editor_check_state::failed,
            "the player did not start: " + error,
            app.tools.player.path.string());
        return err;
    }
    r.player = std::move(player);
    r.player_candidate = static_cast<int>(r.selected);
    r.player_started = std::chrono::steady_clock::now();
    r.player_stopped = false;
    r.player_output.clear();
    r.player_out_partial.clear();
    r.player_err_partial.clear();
    rv_editor_check_set(c,
        rv_editor_check_player,
        rv_editor_check_state::running,
        "pid " + std::to_string(r.player->pid()),
        app.tools.player.path.string());
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::info,
        "player started on candidate #" + std::to_string(c.number) + ": pid " + std::to_string(r.player->pid()),
        rv_editor_log_channel::none,
        r.player->pid(),
        c.number);
    return RV_OK;
}

int rv_editor_app_stop_player(rv_editor_app &app)
{
    const bool running = rv_editor_app_player_running(app);
    if (!running) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "player not stopped: no player runs");
        return RV_ERR_INVAL;
    }
    rv_editor_release &r = app.release;
    // The first ask is a polite SIGTERM; asked again, it is killed. Marked even when the signal fails,
    // so the next ask goes to SIGKILL.
    const int stop_err = r.player->stop(r.player_stopped);
    r.player_stopped = true;
    const bool delivered = stop_err == RV_OK || stop_err == RV_ERR_ALREADY;
    if (!delivered) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "player stop failed: signal not delivered, code " + std::to_string(stop_err),
            rv_editor_log_channel::none,
            r.player->pid(),
            rv_editor_player_run(r));
        return stop_err;
    }
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::warning,
        "player stopped by the operator",
        rv_editor_log_channel::none,
        r.player->pid(),
        rv_editor_player_run(r));
    return RV_OK;
}

bool rv_editor_app_player_running(const rv_editor_app &app)
{
    const rv_editor_release &r = app.release;
    return r.player != nullptr && r.player->running();
}

int rv_editor_app_player_transfer(rv_editor_app &app)
{
    rv_editor_release &r = app.release;
    if (r.player == nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "player output not transferred: no player");
        return RV_ERR_INVAL;
    }
    // Read every frame, exited or not: the output is complete only after the read reaches its end.
    std::string out;
    std::string err;
    std::string read_error;
    const int read_err = r.player->read(out, err, read_chunk_bytes, read_error);
    if (read_err != RV_OK) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "player output not read: " + read_error,
            rv_editor_log_channel::none,
            r.player->pid(),
            rv_editor_player_run(r));
    }
    rv_editor_player_lines(app, rv_editor_log_channel::out, r.player_out_partial, out);
    rv_editor_player_lines(app, rv_editor_log_channel::err, r.player_err_partial, err);
    return read_err;
}

bool rv_editor_app_player_output_complete(rv_editor_app &app)
{
    rv_editor_release &r = app.release;
    return r.player != nullptr && r.player->poll() && r.player->output_done();
}

} // namespace rv_editor
