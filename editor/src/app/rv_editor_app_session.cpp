// Runs a session: state directory, reset of the last session, launch, runtime start, build map and session log.

#include "app/rv_editor_app.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

// Default memory card file name in the state directory.
constexpr std::string_view default_memcard_name = "memcard.mppccard";

// Creates the state directory; without it the session has nowhere to write.
int rv_editor_app_state_dir_prepare(rv_editor_app &app)
{
    if (!app.project.open) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "cannot prepare the state directory: no project is open");
        return RV_ERR_INVAL;
    }
    if (app.project.state_dir.empty()) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "cannot prepare the state directory: no state directory set");
        return RV_ERR_NOENT;
    }
    std::error_code ec;
    std::filesystem::create_directories(app.project.state_dir, ec);
    if (ec) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "cannot prepare the state directory: " + app.project.state_dir.string() + ": " + ec.message());
        return RV_ERR_IO;
    }
    return RV_OK;
}

// Resets session state from the previous run.
void rv_editor_app_session_reset(rv_editor_app &app, const rv_editor_artifact &artifact)
{
    app.build.prune(artifact.dir);
    app.marks.clear();
    app.session_first_seq = app.log.revision() + 1;
    app.observe.read_frame = -1;
}

// Label the log shows for a run: the active profile's name, or the candidate's own card.
std::string rv_editor_app_profile_label(const rv_editor_app &app, const std::filesystem::path &card)
{
    return card.empty() ? app.run_config.profiles[app.run_config.active].name : "the candidate's own";
}

// Builds what a session runs from app state, the artifact and an optional card path (empty: the profile's).
rv_editor_session_launch
rv_editor_app_launch_prepare(const rv_editor_app &app, const rv_editor_artifact &artifact, const std::filesystem::path &card)
{
    const rv_editor_run_profile none;
    // A development run follows the active profile; a candidate, with its own card, does not.
    const rv_editor_run_profile &run = card.empty() ? app.run_config.profiles[app.run_config.active] : none;

    const std::filesystem::path runtime = rv_editor_run_profile_path(run.runtime, app.project.root);
    const std::filesystem::path own_card = rv_editor_run_profile_path(run.memcard, app.project.root);
    const std::filesystem::path run_cwd = rv_editor_run_profile_path(run.cwd, app.project.root);

    rv_editor_session_launch launch;
    launch.console = runtime.empty() ? app.tools.console.path : runtime;
    launch.disc_dir = artifact.dir;
    launch.memcard = !card.empty() ? card :
        !own_card.empty()          ? own_card :
                                     app.project.state_dir / std::string(default_memcard_name);
    launch.cwd = run_cwd.empty() ? app.project.root : run_cwd;
    launch.build_number = artifact.number;
    launch.options = rv_editor_run_profile_args(run);
    launch.env = run.env;
    return launch;
}

// Starts the runtime on a ready launch.
int rv_editor_app_start(rv_editor_app &app, const rv_editor_session_launch &launch)
{
    std::string error;
    const int err = app.session.start(launch, app.log, error);
    if (err != RV_OK) {
        app.log.add(rv_editor_log_source::editor, rv_editor_log_level::error, "cannot start the runtime: " + error);
        return err;
    }
    return RV_OK;
}

// Reads the build map of the artifact; RV_ERR_NOENT when there is none.
int rv_editor_app_build_map_load(rv_editor_app &app, const rv_editor_artifact &artifact)
{
    app.build_map = rv_editor_build_map_read(rv_editor_build_map_path(artifact.dir));
    return app.build_map.empty() ? RV_ERR_NOENT : RV_OK;
}

} // namespace

int rv_editor_app_run_session(rv_editor_app &app, const rv_editor_artifact &artifact, const std::filesystem::path &card)
{
    const int state_err = rv_editor_app_state_dir_prepare(app);
    if (state_err != RV_OK) {
        return state_err;
    }
    rv_editor_app_session_reset(app, artifact);

    const rv_editor_session_launch launch = rv_editor_app_launch_prepare(app, artifact, card);
    const std::string profile = rv_editor_app_profile_label(app, card);
    app.session_profile = profile;
    // Log profile info when running from development (not candidate) card.
    if (card.empty()) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::info,
            "run profile " + profile + ": build #" + std::to_string(artifact.number));
    }
    // A live session answers BUSY from start; that reason wins over a missing console.
    const bool console_missing = launch.console.empty() && !app.session.live();
    if (console_missing) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::error,
            "cannot start the runtime: no console found; search for tools in Settings");
        return RV_ERR_NOENT;
    }

    const int start_err = rv_editor_app_start(app, launch);
    // A live runtime that failed its handshake keeps its output in the session log; BUSY means another session
    // is running and its log must stay as it is.
    const bool failed_live = start_err != RV_OK && start_err != RV_ERR_BUSY && app.session.live();
    if (failed_live) {
        rv_editor_app_attach_session_log(app);
    }
    if (start_err != RV_OK) {
        return start_err;
    }

    // A missing map is only a warning: reload targets fall back to the entry script.
    const int map_err = rv_editor_app_build_map_load(app, artifact);
    if (map_err != RV_OK) {
        app.log.add(rv_editor_log_source::editor,
            rv_editor_log_level::warning,
            rv_editor_build_map_path(artifact.dir).string() + ": no map; reload targets fall back to the entry script");
    }

    rv_editor_app_attach_session_log(app);
    return RV_OK;
}

} // namespace rv_editor
