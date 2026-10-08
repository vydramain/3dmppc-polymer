// What the editor's commands do to its models, and why one cannot run now.

#include "app/rv_editor_app.hpp"

#include <chrono>
#include <system_error>
#include <thread>

#include "pdk/rv_err.h"

#include "project/rv_editor_templates.hpp"
#include "release/rv_editor_candidate_store.hpp"

namespace rv_editor
{

namespace
{

constexpr auto shutdown_wait_timeout = std::chrono::seconds(4);
constexpr auto shutdown_poll_interval = std::chrono::milliseconds(20);
// Cache subdirectory holding release candidates and their logs.
constexpr std::string_view candidates_dir_name = "candidates";
// Default memory card file name in the state directory.
constexpr std::string_view default_memcard_name = "memcard.mppccard";

void rv_editor_app_tool_note(rv_editor_app &app, const char *name, const rv_editor_tool &tool)
{
    const bool ok = tool.problem.empty();
    const rv_editor_log_level level = ok ? rv_editor_log_level::info : rv_editor_log_level::warning;
    const std::string version_suffix = ok && !tool.version.empty() ? ", " + tool.version : "";
    const std::string detail = ok ? tool.path.string() + " (" + tool.origin + ")" + version_suffix : tool.problem;
    app.log.add(rv_editor_log_source::editor, level, std::string(name) + ": " + detail);
}

// Resets session state from the previous run.
void rv_editor_app_session_reset(rv_editor_app &app, const rv_editor_artifact &artifact)
{
    app.build.prune(artifact.dir);
    // A new console starts with every key up: no capture carries over.
    app.game_captured = false;
    app.marks.clear();
    app.session_first_seq = app.log.revision() + 1;
    app.observe.read_frame = -1;
}

// Launch parameters for a runtime session.
struct rv_editor_launch {
    std::filesystem::path console;
    std::filesystem::path disc_dir;
    std::filesystem::path memcard;
    std::filesystem::path cwd;
    uint32_t number;
    std::vector<std::string> args;
    std::vector<std::string> env;
    std::string profile;
};

// Prepares launch parameters from app state, artifact and optional card path.
rv_editor_launch
rv_editor_app_launch_params(const rv_editor_app &app, const rv_editor_artifact &artifact, const std::filesystem::path &card)
{
    rv_editor_launch launch;
    const rv_editor_run_profile none;
    // A development run follows the active profile; a candidate, with its own card, does not.
    const rv_editor_run_profile &profile = card.empty() ? app.run_config.profiles[app.run_config.active] : none;

    const std::filesystem::path runtime = rv_editor_run_profile_path(profile.runtime, app.project.root);
    const std::filesystem::path own_card = rv_editor_run_profile_path(profile.memcard, app.project.root);
    const std::filesystem::path profile_cwd = rv_editor_run_profile_path(profile.cwd, app.project.root);

    launch.console = runtime.empty() ? app.tools.console.path : runtime;
    launch.disc_dir = artifact.dir;
    launch.memcard = !card.empty() ? card :
        !own_card.empty()          ? own_card :
                                     app.project.state_dir / std::string(default_memcard_name);
    launch.cwd = profile_cwd.empty() ? app.project.root : profile_cwd;
    launch.number = artifact.number;
    launch.args = rv_editor_run_profile_args(profile);
    launch.env = profile.env;
    launch.profile = card.empty() ? profile.name : "the candidate's own";

    return launch;
}

// Reads the build map from the artifact directory.
void rv_editor_app_build_map_load(rv_editor_app &app, const rv_editor_artifact &artifact)
{
    const std::filesystem::path map_path = rv_editor_build_map_path(artifact.dir);
    app.build_map = rv_editor_build_map_read(map_path);
    if (app.build_map.empty()) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            map_path.string() + ": no map; reload targets fall back to the entry script");
    }
}

} // namespace

int rv_editor_app_start(rv_editor_app &app, const rv_editor_artifact &artifact, const std::filesystem::path &card)
{
    // Precondition: state directory must be known.
    if (app.project.state_dir.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot start the runtime: no state directory");
        return RV_ERR_NOENT;
    }

    // Ensure state directory exists.
    std::error_code ec;
    std::filesystem::create_directories(app.project.state_dir, ec);
    if (ec) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            app.project.state_dir.string() + ": " + ec.message());
        return RV_ERR_IO;
    }

    // Reset session state from previous run.
    rv_editor_app_session_reset(app, artifact);

    // Prepare launch parameters.
    const rv_editor_launch params = rv_editor_app_launch_params(app, artifact, card);
    app.session_profile = params.profile;

    // Log profile info when running from development (not candidate) card.
    if (card.empty()) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::info,
            "run profile " + params.profile + ": build #" + std::to_string(artifact.number));
    }

    // Start the runtime session.
    std::string error;
    const int err = app.session.start(params.console,
        params.disc_dir,
        params.memcard,
        params.cwd,
        params.number,
        params.args,
        params.env,
        app.log,
        error);

    if (err != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot start the runtime: " + error);
        return err;
    }

    // Load build map.
    rv_editor_app_build_map_load(app, artifact);

    rv_editor_app_attach_session_log(app);
    return RV_OK;
}

void rv_editor_app_init(rv_editor_app &app)
{
    app.tools = rv_editor_toolchain_find();
    if (!app.tools.settings_error.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, app.tools.settings_error);
    }
    rv_editor_app_tool_note(app, "runtime", app.tools.console);
    rv_editor_app_tool_note(app, "burner", app.tools.burner);
    rv_editor_app_tool_note(app, "baker", app.tools.baker);
}

int rv_editor_app_open(rv_editor_app &app, const std::filesystem::path &target)
{
    std::string error;
    rv_editor_project project;
    const int err = rv_editor_project_open(target, project, error);
    if (err != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot open: " + error);
        return err;
    }
    app.project = std::move(project);
    // Candidates, Observe's pins and findings belong to the project they came from.
    app.release = {};
    app.observe = {};
    app.findings = {};
    app.scene_tabs = {};
    // Another project takes the keyboard back from the game.
    app.game_captured = false;
    app.session.pad(0, app.log);
    app.files.open(app.project.root, app.log);
    std::vector<std::string> unread;
    app.release.candidates = rv_editor_candidates_load(app.project.cache_dir / std::string(candidates_dir_name), unread);
    for (const std::string &why : unread) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, "candidate record not read: " + why);
    }
    // Their identity is checked again: the bytes may have changed while no window watched.
    for (rv_editor_candidate &c : app.release.candidates) {
        rv_editor_candidate_hash(c);
    }
    app.release.selected = app.release.candidates.empty() ? 0 : app.release.candidates.size() - 1;
    rv_editor_app_scene_first(app);
    app.run_config = rv_editor_run_config_load(app.project.root);
    if (!app.run_config.error.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, app.run_config.error);
    }
    app.run_problem = rv_editor_run_profile_problem(app.run_config.profiles[app.run_config.active], app.project.root);
    ++app.run_config_revision;
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "opened " + app.project.root.string());
    rv_editor_recent_add(app.project.root);
    if (!app.project.manifest_error.empty()) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, app.project.manifest_error);
    }
    return RV_OK;
}

std::vector<int64_t> rv_editor_app_unsaved(const rv_editor_app &app)
{
    std::vector<int64_t> ids;
    for (const rv_editor_nvim_buffer &b : app.nvim.modified()) {
        if (!b.name.empty()) {
            ids.push_back(b.id);
        }
    }
    return ids;
}

bool rv_editor_app_run_builds(const rv_editor_app &app)
{
    return app.inputs_changed || app.build.dev_state() != rv_editor_build_state::succeeded || !app.build.last_success();
}

int rv_editor_app_build(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_build(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not built: ") + why);
        return RV_ERR_BUSY;
    }
    if (!rv_editor_app_unsaved(app).empty() || rv_editor_app_scene_dirty(app)) {
        app.unsaved_ask = rv_editor_unsaved_ask::build;
        return RV_ERR_BUSY;
    }
    return rv_editor_app_build_saved(app);
}

int rv_editor_app_build_saved(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_build(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not built: ") + why);
        return RV_ERR_BUSY;
    }
    std::string error;
    app.build_first_seq = app.log.revision() + 1;
    app.run_after_build = false;
    app.restart_after_build = false;
    app.restart_after_stop = false;
    const int err = app.build.start(app.project, app.tools, app.log, error);
    if (err != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot build: " + error);
        return err;
    }
    rv_editor_app_attach_build_log(app);
    app.inputs_changed = false;
    return RV_OK;
}

int rv_editor_app_run(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_run(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not run: ") + why);
        return RV_ERR_BUSY;
    }
    if (app.session.state() == rv_editor_run_state::paused) {
        return app.session.resume(app.log);
    }
    if (!rv_editor_app_unsaved(app).empty() || rv_editor_app_scene_dirty(app)) {
        app.unsaved_ask = rv_editor_unsaved_ask::run;
        return RV_ERR_BUSY;
    }
    return rv_editor_app_run_saved(app);
}

int rv_editor_app_run_saved(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_run(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not run: ") + why);
        return RV_ERR_BUSY;
    }
    app.restart_after_build = false;
    app.restart_after_stop = false;
    if (!rv_editor_app_run_builds(app)) {
        return rv_editor_app_start(app, *app.build.last_success());
    }
    const int err = rv_editor_app_build_saved(app);
    app.run_after_build = app.build.busy();
    return err;
}

int rv_editor_app_profiles_save(rv_editor_app &app)
{
    app.run_config.error.clear();
    app.run_problem = rv_editor_run_profile_problem(app.run_config.profiles[app.run_config.active], app.project.root);
    ++app.run_config_revision;
    std::string error;
    const int err = rv_editor_run_config_save(app.project.root, app.run_config, error);
    if (err != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "run profiles not saved: " + error);
        return err;
    }
    return RV_OK;
}

int rv_editor_app_run_last(rv_editor_app &app)
{
    const char *why = rv_editor_app_why_not_run_last(app);
    if (why != nullptr) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::warning, std::string("not run: ") + why);
        return RV_ERR_BUSY;
    }
    const rv_editor_artifact artifact = *app.build.last_success();
    app.log.add(rv_editor_log_source::editor,
        rv_editor_log_level::warning,
        "running the last successful build, #" + std::to_string(artifact.number) +
            ": its native code with the current scripts, assets and scenes");
    return rv_editor_app_start(app, artifact);
}

int rv_editor_app_pause(rv_editor_app &app)
{
    return app.session.pause(app.log);
}

int rv_editor_app_step(rv_editor_app &app)
{
    return app.session.step(app.log);
}

int rv_editor_app_stop(rv_editor_app &app)
{
    return app.session.stop(app.log);
}

int rv_editor_app_rename(rv_editor_app &app, const std::filesystem::path &from, const std::string &name, std::string &error)
{
    error = app.nvim.buffers_unsaved_at(from);
    if (!error.empty()) {
        return RV_ERR_BUSY;
    }
    const std::filesystem::path to = from.parent_path() / name;
    const int err = app.files.rename(from, name, error);
    if (err != RV_OK) {
        return err;
    }
    app.nvim.rename_buffers_at(from, to);
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "renamed " + from.string() + " to " + name);
    return RV_OK;
}

int rv_editor_app_remove(rv_editor_app &app, const std::filesystem::path &path, std::string &error)
{
    error = app.nvim.buffers_unsaved_at(path);
    if (!error.empty()) {
        return RV_ERR_BUSY;
    }
    const int err = app.files.remove(path, error);
    if (err != RV_OK) {
        return err;
    }
    app.nvim.delete_buffers_at(path);
    app.log.add(rv_editor_log_source::editor, rv_editor_log_level::info, "deleted " + path.string());
    return RV_OK;
}

void rv_editor_app_shutdown(rv_editor_app &app)
{
    app.build.cancel();
    const auto until = std::chrono::steady_clock::now() + shutdown_wait_timeout;
    while (app.build.busy() && std::chrono::steady_clock::now() < until) {
        app.build.update(app.log);
        std::this_thread::sleep_for(shutdown_poll_interval);
    }
    app.session.shutdown(app.log);
    app.nvim.stop();
    app.terminals.clear(); // each shell hangs up and is reaped
}

} // namespace rv_editor
