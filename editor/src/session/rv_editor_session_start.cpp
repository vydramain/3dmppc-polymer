// Starting one runtime session: the console's command line, the process, the handshake.

#include "session/rv_editor_session.hpp"

#include <string>
#include <vector>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

constexpr std::string_view flag_dev = "--dev";
constexpr std::string_view flag_frame_fd = "--frame-fd";
constexpr std::string_view flag_memcard = "--memcard";

constexpr int fd_frame_descriptor = 3;

// The console's command line; the same vector is spawned and written to the log.
int console_argv(const rv_editor_session_launch &launch, std::vector<std::string> &argv, std::string &error)
{
    if (launch.console.empty()) {
        error = "no runtime console to start";
        return RV_ERR_NOENT;
    }
    argv = { launch.console.string(),
        std::string(flag_dev),
        std::string(flag_frame_fd),
        std::to_string(fd_frame_descriptor),
        std::string(flag_memcard),
        launch.memcard.string() };
    argv.insert(argv.end(), launch.options.begin(), launch.options.end());
    argv.push_back(launch.disc_dir.string());
    return RV_OK;
}

std::string join_command(const std::vector<std::string> &argv)
{
    std::string line;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) {
            line += ' ';
        }
        line += argv[i];
    }
    return line;
}

} // namespace

int rv_editor_session::start(const rv_editor_session_launch &launch, rv_editor_log &log, std::string &error)
{
    if (live()) {
        error = "a runtime is already running in this window";
        return RV_ERR_BUSY;
    }
    const int err_frame = frame_mem_.create(error);
    if (err_frame != RV_OK) {
        return err_frame;
    }
    std::vector<std::string> argv;
    const int err_argv = console_argv(launch, argv, error);
    if (err_argv != RV_OK) {
        return err_argv;
    }
    const int err_spawn = spawn(argv, launch, error);
    if (err_spawn != RV_OK) {
        return err_spawn;
    }
    reset_state(launch);
    log.add(rv_editor_log_source::editor,
        rv_editor_log_level::info,
        "runtime started, pid " + std::to_string(proc_.pid()) + ": " + join_command(argv),
        rv_editor_log_channel::none,
        proc_.pid(),
        number_);
    const int err_handshake = handshake(log, error);
    if (err_handshake != RV_OK) {
        return err_handshake;
    }
    return RV_OK;
}

int rv_editor_session::spawn(const std::vector<std::string> &argv, const rv_editor_session_launch &launch, std::string &error)
{
    // The console writes its frames into frame_mem_, handed over as the frame descriptor.
    return proc_.start(argv, launch.cwd, error, frame_mem_.fd(), launch.env);
}

void rv_editor_session::reset_state(const rv_editor_session_launch &launch)
{
    parser_ = {};
    pending_.clear();
    next_id_ = 1;
    frame_ = 0;
    pad_sent_ = 0;
    uncertain_ = false;
    quit_sent_ = false;
    forced_ = false;
    input_full_ = false;
    handshake_done_ = false;
    channel_open_ = true;
    build_number_ = launch.build_number;
    ++number_;
    reloading_ = false;
    reload_id_ = 0;
    reload_result_.clear();
    facts_ = {};
    answers_.clear();
    scenes_read_.clear();
    started_wall_ = std::chrono::system_clock::now();
    ended_wall_ = {};
    disc_dir_ = launch.disc_dir;
    console_ = launch.console;
    last_confirmed_ = {};
    in_flight_.clear();
    end_reason_.clear();
    refusal_.clear();
    eof_at_ = {};
    err_partial_.clear();
    out_partial_.clear();
    started_ = std::chrono::steady_clock::now();
    state_ = rv_editor_run_state::starting;
    active_ = true;
}

int rv_editor_session::handshake(rv_editor_log &log, std::string &error)
{
    // Nothing is enabled until this answers.
    int64_t id = 0;
    std::string send_error;
    if (send(std::string(cmd_status), log, id, send_error) == RV_OK) {
        return RV_OK;
    }
    error = "cannot send status to the runtime: " + send_error;
    // Left live on purpose: finish() reaps the killed process once its output ends.
    const int stop_result = force_stop(log);
    if (stop_result != RV_OK && stop_result != RV_ERR_ALREADY) {
        error += "; the runtime was not stopped";
    }
    return RV_ERR_IO;
}

} // namespace rv_editor
