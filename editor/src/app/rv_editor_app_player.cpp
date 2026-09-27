// The player: a 3dmppc built without devtools plays a candidate's image in its
// own window (REL-06). The editor sees its PID, its output and how it ended, and
// nothing else: no pause, step or reload reaches it (REL-07).

#include "app/rv_editor_app.hpp"

#include <chrono>
#include <string>

#include "project/rv_editor_toml.hpp"
#include "release/rv_editor_candidate_store.hpp"

namespace rv_editor
{

namespace
{

// Its output as log lines, each whole; a line still being written waits.
void rv_editor_player_lines(rv_editor_app &app, const std::string &bytes)
{
    rv_editor_release &r = app.release;
    r.player_partial += bytes;
    size_t nl = 0;
    while ((nl = r.player_partial.find('\n')) != std::string::npos) {
        const std::string line = r.player_partial.substr(0, nl);
        r.player_partial.erase(0, nl + 1);
        app.log.add(rv_editor_log_source::runtime, rv_editor_log_level::info, "player: " + line);
        r.player_output += line + "\n";
    }
}

void rv_editor_player_finish(rv_editor_app &app)
{
    rv_editor_release &r = app.release;
    rv_editor_player_lines(app, r.player_partial.empty() ? std::string() : "\n");
    const rv_editor_process::rv_editor_exit exit = r.player->exit_status();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
        r.player_started).count();
    const std::string ended = rv_editor_exit_text(exit) + " after " + std::to_string(seconds) + " s";
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "player ended: " + ended);
    if (r.player_candidate >= 0 && static_cast<size_t>(r.player_candidate) < r.candidates.size()) {
        rv_editor_candidate &c = r.candidates[static_cast<size_t>(r.player_candidate)];
        // Stopped by the editor is the operator's act and proves no normal exit (REL-07).
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
        if (!rv_editor_file_replace(rv_editor_candidate_log(dir, c.number, "player-" + std::to_string(k)),
                r.player_output, error)) {
            app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "player log not kept: " + error);
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
    // Its identity is read again while it plays; a difference voids the results (REL-02).
    rv_editor_candidate_hash(c);
    // A card of its own: neither development saves nor the dev playtest's reach it.
    std::filesystem::path card = c.image;
    card.replace_extension(".player.mppccard");
    auto player = std::make_unique<rv_editor_process>();
    std::string error;
    if (!player->start({ app.tools.player.path.string(), "--memcard", card.string(), c.image.string() },
            c.image.parent_path(), error)) {
        rv_editor_check_set(c, rv_editor_check_player, rv_editor_check_state::failed,
            "the player did not start: " + error, app.tools.player.path.string());
        return;
    }
    r.player = std::move(player);
    r.player_candidate = static_cast<int>(r.selected);
    r.player_started = std::chrono::steady_clock::now();
    r.player_stopped = false;
    r.player_output.clear();
    r.player_partial.clear();
    rv_editor_check_set(c, rv_editor_check_player, rv_editor_check_state::running,
        "pid " + std::to_string(r.player->pid()), app.tools.player.path.string());
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info,
        "player started on candidate #" + std::to_string(c.number) + ": pid " + std::to_string(r.player->pid()));
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
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "player stopped by the operator");
}

void rv_editor_app_player_update(rv_editor_app &app)
{
    rv_editor_release &r = app.release;
    if (r.player == nullptr) {
        return;
    }
    std::string out;
    std::string err;
    r.player->read(out, err, 65536);
    rv_editor_player_lines(app, out + err);
    if (r.player->poll()) {
        out.clear();
        err.clear();
        r.player->read(out, err, 65536);
        rv_editor_player_lines(app, out + err);
        rv_editor_player_finish(app);
    }
}

} // namespace rv_editor
