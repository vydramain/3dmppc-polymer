// A background process the editor pumps into its log; stopped by SIGTERM, then SIGKILL after a grace.

#include "build/rv_editor_job.hpp"

#include "pdk/rv_err.h"

#include <cerrno>
#include <string_view>
#include <system_error>

namespace rv_editor
{

namespace
{

// Bytes read from the process per poll.
constexpr size_t read_chunk_bytes = 1 << 20;

// A cancelled process gets this long to stop by itself before it is killed.
constexpr auto rv_editor_cancel_grace = std::chrono::seconds(3);

// Characters that make a line blank, so it does not replace the remembered one.
constexpr std::string_view blank_chars = " \t\r\v\f";

// The current line keeps one byte past the bound: a line of exactly line_max bytes then shows whether
// it ends in '\r', and a longer line is cut without a '\r' test. The line is decided only at its end,
// so the result does not depend on how the reads split it.
constexpr size_t kept_line_bytes = rv_editor_log::line_max + 1;

// Decides a finished line: at most line_max bytes, '\r' dropped only if within the bound, blank lines skipped.
// Limit: blank means blank within the first line_max bytes. A line whose first line_max bytes are whitespace
// is skipped even if text follows; this is accepted, as such lines are not meaningful reasons.
void keep_stderr_line(std::string &line, std::string &last)
{
    if (line.size() > rv_editor_log::line_max) {
        line.resize(rv_editor_log::line_max);
    } else if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line.find_first_not_of(blank_chars) != std::string::npos) {
        last = line;
    }
    line.clear();
}

// Adds bytes to the current line and decides every line that ends in them.
void keep_stderr_stream(std::string &line, std::string_view bytes, std::string &last)
{
    while (!bytes.empty()) {
        const size_t nl = bytes.find('\n');
        const size_t room = kept_line_bytes - line.size();
        line.append(bytes.substr(0, nl).substr(0, room));
        if (nl == std::string_view::npos) {
            return;
        }
        keep_stderr_line(line, last);
        bytes = bytes.substr(nl + 1);
    }
}

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
    err_line_.clear();
    err_last_line_.clear();
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
    keep_stderr_stream(err_line_, err, err_last_line_);

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
    keep_stderr_stream(err_line_, err, err_last_line_);
    log.flush_stream(source_, out_partial_, rv_editor_log_channel::out, pid, run_);
    log.flush_stream(source_, err_partial_, rv_editor_log_channel::err, pid, run_);
    // The run's unterminated tail is decided like a line that ended with '\n'.
    keep_stderr_line(err_line_, err_last_line_);
    ended_ = true;
    return result;
}

} // namespace rv_editor
