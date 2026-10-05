// The player: a 3dmppc built without devtools plays a candidate's image in its
// own window. The editor sees its PID, its output and how it ended, and
// nothing else: no pause, step or reload reaches it.

#include "app/rv_editor_app.hpp"

#include <chrono>
#include <string>

#include "project/rv_editor_toml.hpp"
#include "release/rv_editor_candidate_store.hpp"

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// The candidate's number the player runs, or 0 outside a run.
uint32_t rv_editor_player_run(const rv_editor_release &r)
{
    return r.player_candidate >= 0 && static_cast<size_t>(r.player_candidate) < r.candidates.size()
        ? r.candidates[static_cast<size_t>(r.player_candidate)].number
        : 0;
}

// One stream's output as log lines, each whole; a line still being written waits.
void rv_editor_player_lines(rv_editor_app &app, rv_editor_log_channel channel, std::string &partial,
    const std::string &bytes)
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

void rv_editor_player_finish(rv_editor_app &app)
{
    rv_editor_release &r = app.release;
    const int64_t pid = r.player->pid();
    const uint32_t run = rv_editor_player_run(r);
    rv_editor_player_lines(app, rv_editor_log_channel::out, r.player_out_partial,
        r.player_out_partial.empty() ? std::string() : "\n");
    rv_editor_player_lines(app, rv_editor_log_channel::err, r.player_err_partial,
        r.player_err_partial.empty() ? std::string() : "\n");
    if (r.player->output_cut()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning,
            "player output after exit was not fully read: a process it started kept a pipe open past the grace",
            rv_editor_log_channel::none, pid, run);
    }
    const rv_editor_process::rv_editor_exit exit = r.player->exit_status();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
        r.player_started).count();
    const std::string ended = rv_editor_exit_text(exit) + " after " + std::to_string(seconds) + " s";
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "player ended: " + ended,
        rv_editor_log_channel::none, pid, run);
    if (r.player_candidate >= 0 && static_cast<size_t>(r.player_candidate) < r.candidates.size()) {
        rv_editor_candidate &c = r.candidates[static_cast<size_t>(r.player_candidate)];
        // Stopped by the editor is the operator's act and proves no normal exit.
        const rv_editor_check_state state = r.player_stopped ? rv_editor_check_state::not_run
            : exit.signal == 0 && exit.code == 0             ? rv_editor_check_state::passed
                                                             : rv_editor_check_state::failed;
        const std::string note = r.player_stopped ? "stopped by the operator (" + ended + "): no normal exit shown"
            : state == rv_editor_check_state::passed ? "ended by itself, " + ended
                                                     : "ended " + ended;
        rv_editor_check_set(c, rv_editor_check_player, state, note, app.tools.player.path.string());
        // Its output beside the record, as the first free player-<k>.log.
        const std::filesystem::path dir = app.project.cache_dir / "candidates";
        uint32_t k = 1;
        std::error_code ec;
        while (std::filesystem::exists(rv_editor_candidate_log(dir, c.number, "player-" + std::to_string(k)), ec)) {
            ++k;
        }
        std::string error;
        if (rv_editor_file_replace(rv_editor_candidate_log(dir, c.number, "player-" + std::to_string(k)),
                r.player_output, error) != RV_OK) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "player log not kept: " + error,
                rv_editor_log_channel::none, pid, run);
        }
    }
    r.player.reset();
    r.player_candidate = -1;
}

} // namespace

const char *rv_editor_app_why_not_play(const rv_editor_app &app)
{
    const rv_editor_release &r = app.release;
    if (r.candidates.empty()) {
        return "No candidate yet: Build Candidate first";
    }
    if (r.player != nullptr) {
        return "The player is running";
    }
    const rv_editor_candidate &c = r.candidates[r.selected];
    if (c.bytes_changed) {
        return "The image's bytes changed after it was built";
    }
    if (c.sha256.empty()) {
        return "Hashing the image first";
    }
    if (!app.tools.player.problem.empty()) {
        return app.tools.player.problem.c_str();
    }
    return nullptr;
}

void rv_editor_app_play_candidate(rv_editor_app &app)
{
    if (rv_editor_app_why_not_play(app) != nullptr) {
        return;
    }
    rv_editor_release &r = app.release;
    rv_editor_candidate &c = r.candidates[r.selected];
    // Its identity is read again while it plays; a difference voids the results.
    rv_editor_candidate_hash(c);
    // A card of its own: neither development saves nor the dev playtest's reach it.
    std::filesystem::path card = c.image;
    card.replace_extension(".player.mppccard");
    auto player = std::make_unique<rv_editor_process>();
    std::string error;
    if (player->start({ app.tools.player.path.string(), "--memcard", card.string(), c.image.string() },
            c.image.parent_path(), error) != RV_OK) {
        rv_editor_check_set(c, rv_editor_check_player, rv_editor_check_state::failed,
            "the player did not start: " + error, app.tools.player.path.string());
        return;
    }
    r.player = std::move(player);
    r.player_candidate = static_cast<int>(r.selected);
    r.player_started = std::chrono::steady_clock::now();
    r.player_stopped = false;
    r.player_output.clear();
    r.player_out_partial.clear();
    r.player_err_partial.clear();
    rv_editor_check_set(c, rv_editor_check_player, rv_editor_check_state::running,
        "pid " + std::to_string(r.player->pid()), app.tools.player.path.string());
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
        "player started on candidate #" + std::to_string(c.number) + ": pid " + std::to_string(r.player->pid()),
        rv_editor_log_channel::none, r.player->pid(), c.number);
}

void rv_editor_app_stop_player(rv_editor_app &app)
{
    rv_editor_release &r = app.release;
    if (r.player == nullptr || !r.player->running()) {
        return;
    }
    // The first ask is a polite SIGTERM; asked again, it is killed.
    r.player->stop(r.player_stopped);
    r.player_stopped = true;
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "player stopped by the operator",
        rv_editor_log_channel::none, r.player->pid(), rv_editor_player_run(r));
}

bool rv_editor_app_player_running(const rv_editor_app &app)
{
    const rv_editor_release &r = app.release;
    return r.player != nullptr && r.player->running();
}

void rv_editor_app_player_update(rv_editor_app &app)
{
    rv_editor_release &r = app.release;
    if (r.player == nullptr) {
        return;
    }
    // A live player keeps reading every frame, exited or not: rv_editor_player_finish
    // runs once, only when the player's output is complete (output_done()).
    std::string out;
    std::string err;
    r.player->read(out, err, 65536);
    rv_editor_player_lines(app, rv_editor_log_channel::out, r.player_out_partial, out);
    rv_editor_player_lines(app, rv_editor_log_channel::err, r.player_err_partial, err);
    if (r.player->poll() && r.player->output_done()) {
        rv_editor_player_finish(app);
    }
}

} // namespace rv_editor
