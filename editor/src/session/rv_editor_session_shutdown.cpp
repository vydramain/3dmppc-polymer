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
    stop(log);
    // A failed read does not cut the waits short; the first reason is kept.
    int result = RV_OK;
    const auto keep_first = [&result, &error](int step, const std::string &step_error) {
        if (result == RV_OK && step != RV_OK) {
            result = step;
            error = step_error;
        }
    };
    const auto until = std::chrono::steady_clock::now() + shutdown_timeout;
    while (live() && proc_.running() && std::chrono::steady_clock::now() < until) {
        std::string step_error;
        const int step = update(log, step_error);
        keep_first(step, step_error);
        std::this_thread::sleep_for(shutdown_poll_interval);
    }
    if (proc_.running()) {
        force_stop(log);
    }
    // Bounded even when a grandchild keeps a pipe open past output_done()'s grace:
    // shutdown never hangs.
    const auto grace_until = std::chrono::steady_clock::now() + shutdown_grace_timeout;
    while (live() && std::chrono::steady_clock::now() < grace_until) {
        std::string step_error;
        const int step = update(log, step_error);
        keep_first(step, step_error);
        std::this_thread::sleep_for(shutdown_poll_interval);
    }
    if (live()) {
        std::string step_error;
        const int step = finish(log, step_error);
        keep_first(step, step_error);
    }
    if (result != RV_OK) {
        return result;
    }
    return RV_OK;
}

} // namespace rv_editor
