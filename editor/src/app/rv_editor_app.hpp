#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "app/rv_editor_app_findings.hpp"
#include "app/rv_editor_app_parts.hpp"
#include "ui/rv_editor_field_sizes.hpp"
#include "build/rv_editor_build.hpp"
#include "build/rv_editor_build_map.hpp"
#include "files/rv_editor_files.hpp"
#include "layout/rv_editor_tile.hpp"
#include "log/rv_editor_log.hpp"
#include "log/rv_editor_log_file.hpp"
#include "nvim/rv_editor_nvim.hpp"
#include "project/rv_editor_change.hpp"
#include "project/rv_editor_problems.hpp"
#include "project/rv_editor_project.hpp"
#include "project/rv_editor_run_profile.hpp"
#include "project/rv_editor_search.hpp"
#include "scene/rv_editor_scene_edit.hpp"
#include "session/rv_editor_session.hpp"
#include "terminal/rv_editor_terminal.hpp"

namespace rv_editor
{

// Everything one editor window works with. The models live here, outside the
// tile tree; panes only look at them.
struct rv_editor_app {
    rv_editor_toolchain tools;
    rv_editor_project project;
    rv_editor_log log;
    // Writes the chosen processes' lines to files; the app decides which (rv_editor_app_logs.cpp).
    rv_editor_log_file log_files;
    rv_editor_build build;
    rv_editor_session session;
    rv_editor_files files;
    std::map<rv_editor_pane_id, rv_editor_output_view> outputs;
    std::map<rv_editor_pane_id, rv_editor_files_view> files_views;
    // Files the user asked to open (Files, a new file, Problems, Search Results),
    // for the code editor to take, each at its line (0: where nvim last had it).
    std::vector<rv_editor_open_request> open_requests;
    // Operations the panes asked for, run by the app's frame at its start; a request
    // queued while the frame runs waits for the next one.
    std::vector<rv_editor_pane_request> requests;
    // Binary files among them the user asked to see as text anyway (Files > Open as Text).
    std::set<std::filesystem::path> open_as_text;
    std::map<rv_editor_pane_id, rv_editor_code_tabs> code_tabs;
    std::map<rv_editor_pane_id, rv_editor_terminal_view> terminals;
    // Shells of closed tiles, hung up and stepped each frame until they have ended.
    std::vector<std::unique_ptr<rv_editor_pty>> stopping_ptys;
    rv_editor_nvim nvim;
    // A pane's Open Project button was clicked: the window shows its folder dialog.
    bool open_folder_request = false;
    rv_editor_observe observe;
    rv_editor_findings findings;
    rv_editor_release release;
    // The layout in front, for panes that differ by it (Toolchest, Game in Burn).
    rv_editor_layout_preset preset = rv_editor_layout_preset::code;
    // Burn's Candidate & Verify is in front: panes shared with Debug speak of candidates.
    bool release_view = false;
    // Asked by a pane, done by the window next frame: a pane to bring forward, a new
    // Untitled buffer.
    rv_editor_pane_kind show_request = rv_editor_pane_kind::empty;
    // The selection a Files operation leaves (a created file, a renamed entry's new path; an empty
    // path after a delete); the shell moves it into its UI state.
    std::optional<std::filesystem::path> files_select;
    // A double click in Assets asked for this layout; nullopt: none (see rv_editor_shell_take_layout_request).
    std::optional<rv_editor_layout_preset> layout_request;
    bool new_file_request = false;
    // The session in Debug: the profile it started with, and Mark Moment's marks,
    // "frame 812: the door opens late".
    std::string session_profile;
    std::vector<std::string> marks;
    // The first log line of the latest build job, for its diagnostics.
    uint64_t build_first_seq = 0;
    // What that job's lines name, re-read when the log changes; and when the job ended.
    std::vector<rv_editor_problem> problems;
    uint64_t problems_revision = 0;
    std::filesystem::file_time_type build_ended{};
    rv_editor_search_view project_search;
    // A saved project file changed since the latest development build started, so
    // Run builds first; unknown at start, so true. Run started that build: it runs next.
    bool inputs_changed = true;
    bool run_after_build = false;
    // Build or Run met unsaved named buffers: the window asks before either goes ahead.
    rv_editor_unsaved_ask unsaved_ask = rv_editor_unsaved_ask::none;
    // The action whose unsaved-files question is open; set by the shell when it opens
    // the question, cleared when the question closes.
    rv_editor_unsaved_ask unsaved_open = rv_editor_unsaved_ask::none;
    // The first log line of the current session, for a finding's log.
    uint64_t session_first_seq = 0;
    // Run profiles, what stops the active one, and a count of their changes, which the
    // Run Configuration pane refills its form from; Apply and Restart waits for the session to end, then runs.
    rv_editor_run_config run_config;
    std::string run_problem;
    uint64_t run_config_revision = 1;
    bool run_after_stop = false;
    // Build and Restart: a build runs for it; once it ends, the live session (if any)
    // stops, then the new build starts. restart_after_stop is the wait for that stop.
    bool restart_after_build = false;
    bool restart_after_stop = false;
    // The scene the Scene layout edits, if one is open, and why the last open failed.
    std::unique_ptr<rv_editor_scene_doc> scene;
    std::string scene_error;
    // Why the last scene move was refused; empty after a move that went through, a dismiss, or opening a scene.
    std::string scene_move_note;
    rv_editor_assets_ui assets_ui;
    // The started build's map (rv_editor_build_map_read), read at rv_editor_app_run_session;
    // empty once the session is not live.
    std::map<std::string, rv_editor_map_entry> build_map;
    // The file the last-used Code tile shows; the window sets this.
    std::filesystem::path code_file;
    // Reload On Save: files queued to reload, oldest first, no duplicates; reload_due
    // is when the queue may start draining, zero: none due.
    std::vector<std::filesystem::path> reload_queue;
    std::chrono::steady_clock::time_point reload_due{};
    // Reload on a texture: the running bake, if any, and the last one's outcome.
    rv_editor_texture_bake texture_bake;
};

// Scene (editor/src/app/rv_editor_app_scene.cpp): the project's scenes/*.scene.toml,
// the open one, a new one under a free name, and its save.
std::vector<std::filesystem::path> rv_editor_app_scene_files(const rv_editor_app &app);
bool rv_editor_app_scene_dirty(const rv_editor_app &app);
std::string rv_editor_app_scene_name(const rv_editor_app &app);
// If the open scene is unsaved: when path is that same scene, does nothing; else
// leaves it open and sets scene_error naming both, refusing to open path.
// RV_OK when opened; RV_ERR_ALREADY when path is the open scene with unsaved edits (nothing done);
// RV_ERR_BUSY when an unsaved scene refuses; the load's RV_ERR_* otherwise. ALREADY writes its own
// warning to the log.
int rv_editor_app_scene_open(rv_editor_app &app, const std::filesystem::path &path);
// The next free name ("main", else "sceneN") a new scene would get today.
std::string rv_editor_app_scene_free_name(const rv_editor_app &app);

// Creates scenes/<name>.scene.toml (name: non-empty, letters/digits/_/-, must not
// exist yet), puts it on the disc if no pattern already matches it, writes
// src/<id>_scene.hpp when write_cpp and the disc is C++, then opens it. RV_OK on success,
// RV_ERR_INVAL if name is invalid, RV_ERR_* from scene_save, code from manifest_add_pattern if
// disc.toml update fails, or from codegen_write if C++ write fails; the scene file and whatever else
// already succeeded stay. Reason in error.
int rv_editor_app_scene_create(rv_editor_app &app, std::string_view name, bool write_cpp, std::string &error);
// Saves the scene if dirty. RV_OK when successfully saved; RV_ERR_ALREADY when it was not dirty
// (nothing written; the caller says so); RV_ERR_* on save failure.
int rv_editor_app_scene_save(rv_editor_app &app, std::string &error);
// The project's first scene; called when a project opens. RV_ERR_NOENT when the project has
// no scene (not a failure: the caller decides whether to report it); otherwise rv_editor_app_scene_open's code.
int rv_editor_app_scene_first(rv_editor_app &app);
// Moves object `id` under `parent` (empty: the root), keeping its place in the scene when keep_world.
// RV_OK clears scene_move_note. RV_ERR_INVAL when no scene is open (logged) or the move is refused:
// the reason is logged as a warning and set in scene_move_note; the refusal's code is returned.
int rv_editor_app_scene_move(rv_editor_app &app, const std::string &id, const std::string &parent, bool keep_world);

// Writes the profiles to the project. On success checks the active one again and bumps
// run_config_revision, so the pane refills its form; on failure the in-memory state stays as it was.
// RV_OK or RV_ERR_* on save failure.
int rv_editor_app_profiles_save(rv_editor_app &app);
// Run profile ops (editor/src/app/rv_editor_app_profiles.cpp): each writes through
// rv_editor_app_profiles_save and returns its code; a refusal is logged once with its reason.
// A failed save leaves run_config as it was, so memory matches the file.
// The problem of an edited profile, name conflict included; empty when it has none.
std::string rv_editor_app_profile_problem(const rv_editor_app &app, const rv_editor_run_profile &edited);
// RV_ERR_INVAL when index is out of range; RV_ERR_ALREADY when it is the active one (nothing done).
int rv_editor_app_profile_select(rv_editor_app &app, size_t index);
// Copies the active profile under the next free "ProfileN" and makes the copy active.
int rv_editor_app_profile_add(rv_editor_app &app);
// RV_ERR_INVAL when the active profile is the last one.
int rv_editor_app_profile_delete(rv_editor_app &app);
// RV_ERR_INVAL when edited has a problem; nothing changes then.
int rv_editor_app_profile_apply(rv_editor_app &app, const rv_editor_run_profile &edited);
// Apply, then restart the session (rv_editor_app_restart). RV_ERR_INVAL with nothing applied when no session runs;
// a refused apply returns its code and does not restart.
int rv_editor_app_profile_apply_restart(rv_editor_app &app, const rv_editor_run_profile &edited);

// Looks for the tools and says what it found.
void rv_editor_app_init(rv_editor_app &app);

// Opens a game directory or its disc.toml. A running session and build keep
// running on the project they started with until they end. RV_OK on success, RV_ERR_* on failure.
int rv_editor_app_open(rv_editor_app &app, const std::filesystem::path &target);

// Why each action cannot run now, or nullptr when it can. The text
// lives until the next change to `app`.
const char *rv_editor_app_why_not_build(const rv_editor_app &app);
const char *rv_editor_app_why_not_run(const rv_editor_app &app);
const char *rv_editor_app_why_not_run_last(const rv_editor_app &app);
const char *rv_editor_app_why_not_pause(const rv_editor_app &app);
const char *rv_editor_app_why_not_step(const rv_editor_app &app);
const char *rv_editor_app_why_not_stop(const rv_editor_app &app);
// Why Reload cannot run now; nullptr when it can. Only asked when the session can reload at all.
const char *rv_editor_app_why_not_reload(const rv_editor_app &app);
// The running console can reload its entry script: a directory medium and a Lua entry.
bool rv_editor_app_can_reload(const rv_editor_app &app);
// The change classifier (project/rv_editor_change.hpp) against this app's project,
// manifest and running build's map (editor/src/app/rv_editor_app_change.cpp).
rv_editor_change_plan rv_editor_app_change_for(const rv_editor_app &app, const std::filesystem::path &file);
// Reload On Save: `changed` joins the queue when its plan is reload_entry/reload_module.
void rv_editor_app_reload_queue_note(rv_editor_app &app, const std::filesystem::path &changed);
// Reload On Save steps, asked for by rv_editor_app_update (questions there). Drop: no live session
// takes the queue, so it goes; nothing can refuse it.
void rv_editor_app_reload_queue_drop(rv_editor_app &app);
// Question: the settle wait has passed (reload_due is set and reached).
bool rv_editor_app_reload_due(const rv_editor_app &app);
// Question: the session can reload its entry now and no texture bake runs.
bool rv_editor_app_can_reload_now(const rv_editor_app &app);
// Sends the oldest queued file. RV_OK; RV_ERR_INVAL when the queue is empty; RV_ERR_BUSY when a
// reload cannot go now. Both refusals are logged; the queue is kept.
int rv_editor_app_reload_queue_send(rv_editor_app &app);
// Changed files steps, asked for by rv_editor_app_update_changed_files (question there). Applies the
// list: nvim checks, release, inputs, queue notes, manifest reload; then the list is cleared. RV_OK;
// RV_ERR_INVAL when the list is empty (logged).
int rv_editor_app_changed_files_apply(rv_editor_app &app);

// Texture bake (editor/src/app/rv_editor_app_texture.cpp): a refresh_texture reload
// bakes the changed PNG with the burner before sending it, one bake at a time,
// async. True while one runs; when `name` is not null it receives what it bakes.
bool rv_editor_app_texture_bake_busy(const rv_editor_app &app, std::string *name = nullptr);
// Starts baking `name` (the texture's disc name) from `png` into staging; logs and
// does nothing if a bake is already running. RV_OK on success, RV_ERR_BUSY if a bake is running, RV_ERR_IO on filesystem
// failure, or the start error code.
int rv_editor_app_texture_bake_start(rv_editor_app &app, const std::string &name, const std::filesystem::path &png);
// Steps of a running bake, called by rv_editor_app_update, which asks the questions.
// transfer: moves the output read so far to the log. RV_OK, RV_ERR_IO on a failed read (logged),
// RV_ERR_INVAL when no bake runs (logged).
int rv_editor_app_texture_bake_transfer(rv_editor_app &app);
// reap: polls the baker and closes its pipes once they are done; releases nothing. RV_OK once it has
// exited and its output is closed; RV_ERR_BUSY while it runs or its output is still open within the grace (not a failure);
// RV_ERR_INVAL when no bake runs (logged).
int rv_editor_app_texture_bake_reap(rv_editor_app &app);
// end: one last read of what the pipes hold, then flush and judge; releases the baker.
// RV_OK when baked; RV_ERR_IO when the baker failed or its read failed (logged); RV_ERR_INVAL when no bake runs (logged).
int rv_editor_app_texture_bake_end(rv_editor_app &app);
// send: sends the baked bytes over the session's reload slot. Precondition: called only right after
// end judged the bake ok (the frame does it). RV_OK; RV_ERR_BUSY when the session cannot take it now
// (logged as a warning); RV_ERR_IO when the baked file is unreadable; else the reload's code.
int rv_editor_app_texture_bake_send(rv_editor_app &app);
// Question: the judged result, valid after end.
bool rv_editor_app_texture_bake_ok(const rv_editor_app &app);

// Build and Run ask first when a named buffer is unsaved; the _saved
// forms go ahead with the files as they are on disk.
// RV_OK: build started. RV_ERR_PENDING: waits on the unsaved-files answer (unsaved_ask set), nothing started.
// RV_ERR_BUSY: refused; other RV_ERR_*: failed, reason in the log.
int rv_editor_app_build(rv_editor_app &app);
int rv_editor_app_build_saved(rv_editor_app &app);
// Cancel of the running build. RV_OK: stop began. RV_ERR_ALREADY: nothing to stop (logged as a warning);
// other RV_ERR_*: failed, reason in the log.
int rv_editor_app_build_cancel(rv_editor_app &app);
int rv_editor_app_run_saved(rv_editor_app &app);
// Build and Restart (editor/src/app/rv_editor_app_change.cpp): builds, then restarts
// the live session (or just starts one) once the build has ended.
// RV_OK: build started. RV_ERR_PENDING: waits on the unsaved-files answer (unsaved_ask set), nothing started.
// RV_ERR_BUSY: refused; other RV_ERR_*: failed, reason in the log.
int rv_editor_app_build_restart(rv_editor_app &app);
int rv_editor_app_build_restart_saved(rv_editor_app &app);
// Build and Restart steps, asked for by rv_editor_app_update (questions there).
// Question: the build that ended succeeded and has an artifact to run. Run after build asks it too.
bool rv_editor_app_build_restart_built(const rv_editor_app &app);
// Refuse: the build did not succeed, so no restart goes on; logs why. RV_ERR_BUSY.
int rv_editor_app_build_restart_refuse(rv_editor_app &app);
// Stop: asks the live session to stop; the restart waits for it. RV_OK, also when a stop is
// already under way; else the stop's RV_ERR_* (logged, the wait is cleared).
int rv_editor_app_build_restart_stop(rv_editor_app &app);
// Start: runs the last successful build. RV_OK or rv_editor_app_run_session's RV_ERR_*;
// RV_ERR_NOENT when no successful build exists (logged, nothing started).
int rv_editor_app_build_restart_start(rv_editor_app &app);
// Run would build first: nothing succeeded yet, the last build did not, or an input changed.
bool rv_editor_app_run_builds(const rv_editor_app &app);
// Buffers with a file name and unsaved changes: what a build would miss.
std::vector<int64_t> rv_editor_app_unsaved(const rv_editor_app &app);
// Runs a session on `artifact` with memory card `card` (empty: the project's): state directory,
// launch, runtime start, build map, session log. Returns RV_OK on success or RV_ERR_* on failure.
int rv_editor_app_run_session(rv_editor_app &app, const rv_editor_artifact &artifact, const std::filesystem::path &card = {});

// Release (editor/src/app/rv_editor_app_release.cpp): Build Candidate writes a new
// numbered image; Run Candidate runs the one shown on the development console.
// Returns RV_OK on success or RV_ERR_* on failure.
int rv_editor_app_build_candidate(rv_editor_app &app);
const char *rv_editor_app_why_not_run_candidate(const rv_editor_app &app);
// Returns RV_OK on success or RV_ERR_* on failure.
int rv_editor_app_run_candidate(rv_editor_app &app);
// Player (editor/src/app/rv_editor_app_player.cpp): Run in Player plays the shown
// candidate in the player's own window; Stop Player is the operator's act.
const char *rv_editor_app_why_not_play(const rv_editor_app &app);
// Returns RV_OK on success or RV_ERR_* on failure.
int rv_editor_app_play_candidate(rv_editor_app &app);
// Stops the running player: the first ask is a polite SIGTERM, asked again SIGKILL. RV_OK on success.
// RV_ERR_INVAL when no player runs (logged; ask rv_editor_app_player_running first); the signal's
// RV_ERR_* when it is not delivered (logged).
int rv_editor_app_stop_player(rv_editor_app &app);
bool rv_editor_app_player_running(const rv_editor_app &app);
// Reads the player's output into the log. RV_OK, or the read's RV_ERR_*; RV_ERR_INVAL when no player runs (logged).
int rv_editor_app_player_transfer(rv_editor_app &app);
// Question: the player has exited and its output is read to the end. Only meaningful with a player.
// Reaps the exited child and may close the pipes after the grace. Ask it after
// rv_editor_app_player_transfer in the same frame.
bool rv_editor_app_player_output_complete(rv_editor_app &app);
// Finishes the player's run and clears it, even when its log is not kept. RV_OK; RV_ERR_IO when the
// player log cannot be kept (logged); RV_ERR_INVAL when no player runs (logged).
int rv_editor_app_player_finish(rv_editor_app &app);
// Returns RV_OK on success or RV_ERR_* on failure.
int rv_editor_app_export_report(rv_editor_app &app);
// Release steps: the frame rv_editor_app_update_release (app_update.cpp) asks the questions and
// calls the operations, all defined in rv_editor_app_release.cpp.
// Question: a candidate build's end was noted and waits for its revision.
bool rv_editor_app_release_build_noted();
// Notes a candidate build's end. RV_OK; RV_ERR_INVAL when no build runs (logged).
int rv_editor_app_release_note_build_end(rv_editor_app &app);
// The sources' revision of the running candidate build. RV_OK when known; RV_ERR_PENDING while
// the lookup runs; RV_ERR_INVAL when no build runs (logged).
int rv_editor_app_release_revision_poll(rv_editor_app &app);
// Finishes the candidate build and clears the latch. RV_OK: the candidate is added. RV_ERR_NOENT:
// the build did not succeed (last_failure says why). RV_ERR_INVAL when no build runs (logged).
int rv_editor_app_release_finish_build(rv_editor_app &app);
// Saves one candidate's record. RV_OK, or the save's RV_ERR_*; the first failure of a run is logged.
int rv_editor_app_release_save_candidate(rv_editor_app &app, rv_editor_candidate &c);
// Questions: a candidate runs on the development console; that session is live and still
// runs its image; the running candidate's loads check is running.
bool rv_editor_app_release_playing(const rv_editor_app &app);
bool rv_editor_app_release_still_playing(const rv_editor_app &app);
bool rv_editor_app_release_loads_running(const rv_editor_app &app);
// The loads verdict of the playing candidate, asked while it is still playing and connected.
// RV_OK; RV_ERR_INVAL when the session no longer plays the candidate (logged).
int rv_editor_app_release_playtest_verdict(rv_editor_app &app);
// The playtest's end, asked when the session no longer runs the playing candidate. RV_OK; RV_ERR_INVAL
// when none plays (logged).
int rv_editor_app_release_playtest_end(rv_editor_app &app);
// A project file changed: every candidate's sources now differ from the tree.
void rv_editor_app_release_changed(rv_editor_app &app);

// Runs the latest build, building the saved files first when rv_editor_app_run_builds; on a paused session this resumes it.
// RV_OK: run or build started (a rebuild comes first), or pause lifted. RV_ERR_PENDING: waits on the unsaved-files answer
// (unsaved_ask set), nothing started. RV_ERR_BUSY: refused; other RV_ERR_*: failed, reason in the log.
int rv_editor_app_run(rv_editor_app &app);
// Runs the last successful build after a later build failed. Returns RV_OK or RV_ERR_*.
int rv_editor_app_run_last(rv_editor_app &app);
// Build ended steps, asked for by rv_editor_app_update_build_ended (questions there).
// Note: old builds are pruned and the end time is kept.
void rv_editor_app_build_ended_note(rv_editor_app &app);
// Refuse: the build a waiting Run waited for did not succeed; logs why. RV_ERR_BUSY.
int rv_editor_app_run_after_build_refuse(rv_editor_app &app);
// Start: runs the last successful build for a waiting Run. RV_OK or rv_editor_app_run_session's RV_ERR_*;
// RV_ERR_NOENT when no successful build exists (logged, nothing started).
int rv_editor_app_run_after_build_start(rv_editor_app &app);
// RV_OK or the session's RV_ERR_* code.
int rv_editor_app_pause(rv_editor_app &app);
int rv_editor_app_step(rv_editor_app &app);
int rv_editor_app_stop(rv_editor_app &app);
// Force Stop: SIGKILL to the runtime; logs the outcome (warning when there is nothing to stop).
int rv_editor_app_force_stop(rv_editor_app &app);
// Restart: sets run_after_stop, then stops; the frame runs the new build once the session ends.
// RV_ERR_INVAL (logged, nothing done) when no session runs.
int rv_editor_app_restart(rv_editor_app &app);
// Queues a request; RV_ERR_ALREADY (warning logged) when one of its kind already waits.
int rv_editor_app_request(rv_editor_app &app, const rv_editor_pane_request &request);
// Waits in the queue, or (restart) its stop or the run after it still waits (run_after_stop,
// or a run's unsaved-files question asked or open).
bool rv_editor_app_request_pending(const rv_editor_app &app, rv_editor_pane_request_kind kind);
// Runs the request's operation; RV_ERR_INVAL (error logged) for an unknown kind.
int rv_editor_app_request_run(rv_editor_app &app, const rv_editor_pane_request &request);
void rv_editor_app_reload(rv_editor_app &app);

// Creates a file or folder in dir; RV_OK or RV_ERR_* with the reason in error.
int rv_editor_app_create_file(rv_editor_app &app,
    const std::filesystem::path &dir,
    const std::string &name,
    std::string &error);
int rv_editor_app_create_dir(rv_editor_app &app, const std::filesystem::path &dir, const std::string &name, std::string &error);
// Renames or deletes inside the project; RV_OK or RV_ERR_* with the reason in error.
int rv_editor_app_rename(rv_editor_app &app, const std::filesystem::path &from, const std::string &name, std::string &error);
int rv_editor_app_remove(rv_editor_app &app, const std::filesystem::path &path, std::string &error);

// Once a frame, before drawing.
void rv_editor_app_update(rv_editor_app &app);

// Before the window closes: cancels the build and ends the runtime, leaving no
// process behind.
void rv_editor_app_shutdown(rv_editor_app &app);

// Logs (editor/src/app/rv_editor_app_logs.cpp): each session and each
// development build keeps its whole log in a file next to it.
// RV_OK when attached; RV_ERR_NOENT without a state/cache directory, or the open's
// error. A refusal is logged as a warning; the session or build goes on without it.
int rv_editor_app_attach_session_log(rv_editor_app &app);
int rv_editor_app_attach_build_log(rv_editor_app &app);

} // namespace rv_editor
