// A background process the editor pumps into its log; stopped by SIGTERM, then SIGKILL after a grace.

#include "build/rv_editor_job.hpp"

#include "pdk/rv_err.h"

#include <cerrno>
#include <system_error>

namespace rv_editor
{

namespace
{

// Bytes read from the process per poll.
constexpr size_t read_chunk_bytes = 1 << 20;

// A cancelled process gets this long to stop by itself before it is killed.
constexpr auto rv_editor_cancel_grace = std::chrono::seconds(3);

} // namespace

int rv_editor_job::start(const std::vector<std::string> &argv,
    const std::filesystem::path &cwd,
    const std::string &name,
    rv_editor_log_source source,
    uint32_t run,
    std::string &error)
{
    const int err = proc_.start(argv, cwd, error);
    if (err != RV_OK) {
        return err;
    }
    name_ = name;
    source_ = source;
    run_ = run;
    out_partial_.clear();
    err_partial_.clear();
    cancelled_ = false;
    ended_ = false;
    return RV_OK;
}

int rv_editor_job::cancel(std::string &error)
{
    if (cancelled_) {
        error = "the " + name_ + " is already being cancelled";
        return RV_ERR_ALREADY;
    }
    if (ended_) {
        error = "no " + name_ + " is running";
        return RV_ERR_ALREADY;
    }
    cancelled_ = true;
    cancel_at_ = std::chrono::steady_clock::now();
    kill_failure_reported_ = false;
    // A failed SIGTERM still leaves the cancel running: pump() escalates to SIGKILL after the grace.
    const int stop_result = proc_.stop(false);
    if (stop_result == RV_ERR_IO) {
        error = "SIGTERM not delivered: " + std::generic_category().message(errno) + "; SIGKILL follows after the grace";
        return RV_ERR_IO;
    }
    return RV_OK;
}

int rv_editor_job::pump(rv_editor_log &log, std::string &error)
{
    if (ended_) {
        return RV_OK;
    }
    std::string out;
    std::string err;
    const int64_t pid = proc_.pid();
    // The first failure is the one reported: a later one must not hide why the first happened.
    int result = RV_OK;
    const auto note = [&result, &error](int rc, const std::string &why) {
        if (result == RV_OK && rc != RV_OK) {
            result = rc;
            error = why;
        }
    };
    std::string read_error;
    const int read_result = proc_.read(out, err, read_chunk_bytes, read_error);
    note(read_result, read_error);
    log.add_stream(source_, out_partial_, out, rv_editor_log_channel::out, pid, run_);
    log.add_stream(source_, err_partial_, err, rv_editor_log_channel::err, pid, run_);

    if (cancelled_ && std::chrono::steady_clock::now() - cancel_at_ > rv_editor_cancel_grace) {
        const int stop_result = proc_.stop(true);
        // RV_ERR_ALREADY: the process has already ended, nothing to kill.
        if (stop_result != RV_OK && stop_result != RV_ERR_ALREADY && !kill_failure_reported_) {
            kill_failure_reported_ = true;
            note(stop_result, name_ + " not killed after cancel");
        }
    }
    if (!proc_.poll() || !proc_.output_done()) {
        return result;
    }
    // Whatever the pipes still held when output completed.
    out.clear();
    err.clear();
    std::string tail_error;
    const int tail_result = proc_.read(out, err, read_chunk_bytes, tail_error);
    note(tail_result, tail_error);
    log.add_stream(source_, out_partial_, out, rv_editor_log_channel::out, pid, run_);
    log.add_stream(source_, err_partial_, err, rv_editor_log_channel::err, pid, run_);
    log.flush_stream(source_, out_partial_, rv_editor_log_channel::out, pid, run_);
    log.flush_stream(source_, err_partial_, rv_editor_log_channel::err, pid, run_);
    ended_ = true;
    return result;
}

} // namespace rv_editor
