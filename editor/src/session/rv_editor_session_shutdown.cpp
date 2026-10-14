// Ends the runtime session before the editor exits: quit, a wait, then kill.

#include "session/rv_editor_session.hpp"

#include <thread>

#include "pdk/rv_err.h"

namespace rv_editor
{

namespace
{

constexpr auto shutdown_timeout = std::chrono::seconds(2);
constexpr auto shutdown_grace_timeout = std::chrono::milliseconds(2500);
constexpr auto shutdown_poll_interval = std::chrono::milliseconds(20);

} // namespace

int rv_editor_session::shutdown(rv_editor_log &log, std::string &error)
{
    if (!live()) {
        return RV_OK;
    }
    // A failed step does not cut the waits short: the first code is returned, every reason is appended to error.
    int result = RV_OK;
    std::string reasons;
    const auto keep_failure = [&result, &reasons](int step, const std::string &step_error) {
        if (step == RV_OK) {
            return;
        }
        if (result == RV_OK) {
            result = step;
        }
        rv_editor_append_failure(reasons, step_error);
    };
    // ALREADY: a stop is under way or the runtime is gone, nothing to report.
    std::string stop_error;
    const int stop_result = stop(log, stop_error);
    if (stop_result != RV_ERR_ALREADY) {
        keep_failure(stop_result, stop_error);
    }
    const auto until = std::chrono::steady_clock::now() + shutdown_timeout;
    while (live() && proc_.running() && std::chrono::steady_clock::now() < until) {
        std::string step_error;
        const int step = update(log, step_error);
        keep_failure(step, step_error);
        std::this_thread::sleep_for(shutdown_poll_interval);
    }
    if (proc_.running()) {
        std::string force_error;
        const int force_result = force_stop(log, force_error);
        keep_failure(force_result, force_error);
    }
    // Bounded even when a grandchild keeps a pipe open past output_done()'s grace:
    // shutdown never hangs.
    const auto grace_until = std::chrono::steady_clock::now() + shutdown_grace_timeout;
    while (live() && std::chrono::steady_clock::now() < grace_until) {
        std::string step_error;
        const int step = update(log, step_error);
        keep_failure(step, step_error);
        std::this_thread::sleep_for(shutdown_poll_interval);
    }
    if (live()) {
        std::string step_error;
        const int step = finish(log, step_error);
        keep_failure(step, step_error);
    }
    // The session has ended here, so a follow-up still recorded is returned now, as update() would.
    std::string follow_up_error;
    const int follow_up_result = take_follow_up(false, follow_up_error);
    keep_failure(follow_up_result, follow_up_error);
    if (result == RV_OK) {
        return RV_OK;
    }
    error = reasons;
    return result;
}

} // namespace rv_editor
