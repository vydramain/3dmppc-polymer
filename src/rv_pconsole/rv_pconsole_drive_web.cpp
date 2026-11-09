// Browser driver of the frame loop: a JS loop on requestAnimationFrame calls the frame export, so nothing here sleeps.
// Disc frames follow the console's clock, not the monitor's 120/144 Hz refresh; the start screen steps at the native slice.
// Once a loop starts, its unwind never returns (boot_screen_run returns rest()'s result only without a screen).
#include "rv_pconsole/rv_pconsole.hpp"

#include <chrono>
#include <functional>

#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

// Starts the frame loop. Each frame is asked on its own requestAnimationFrame, and the next one only after this
// frame's promise settles: a frame that awaits a pad report holds back the next one.
// clang-format off
EM_JS(void, rv_pconsole_web_start_frames, (void *frame), {
    var tick = function() {
        Module['_rv_pconsole_web_frame'](frame).then(function(more) {
            if (more) {
                requestAnimationFrame(tick);
            }
        }, function(e) {
            // As handleException: exit() and unwind end the run silently. Other errors are rethrown, so the browser
            // logs an unhandled rejection; the loop stops because no next frame is asked.
            if (e === 'unwind' || e instanceof ExitStatus) {
                return;
            }
            throw e;
        });
    };
    requestAnimationFrame(tick);
});
// clang-format on

// JSPI_EXPORTS wraps this in WebAssembly.promising. Returns whether the loop asks for another frame.
extern "C" EMSCRIPTEN_KEEPALIVE bool rv_pconsole_web_frame(void *frame)
{
    return (*static_cast<std::function<bool()> *>(frame))();
}

namespace rv_3dmppc
{
namespace
{

// Frame steps per browser callback. A console that fell behind catches up over several callbacks;
// one long callback would freeze the page. A fixed-step run (-F) has no clock gate, so it gets
// this many frames per callback: no rate, but the page does not freeze.
constexpr int RV_PCONSOLE_WEB_MAX_STEPS_PER_CALL = 4;

// Starts the loop on body, then unwinds the caller's frame. body is held by value in its heap object, so it keeps no
// reference into that frame. What it points to (console, disc, the caller's locals that rest reads) survives: the web
// build compiles without exception flags, so no catch or destructor runs, and the stack pointer is not raised.
[[noreturn]] void run_web_loop(std::function<bool()> *body)
{
    rv_pconsole_web_start_frames(body);
    emscripten_unwind_to_js_event_loop();
}

} // namespace

int64_t rv_pconsole::disc_run(rv_de *disc)
{
    run_state run;
    const int64_t started = run_start(disc, run);
    if (started < 0) {
        return started;
    }

    // Heap: the unwind leaves this frame without running destructors, and the frame export calls the body for as
    // long as the page lives.
    auto *body = new std::function<bool()>([this, disc, run]() mutable -> bool {
        for (int steps = 0; steps < RV_PCONSOLE_WEB_MAX_STEPS_PER_CALL; ++steps) {
            if (run.mode == pacing::clock) {
                const auto now = std::chrono::steady_clock::now();
                // Not yet due: the next frame looks again.
                if (now < run.deadline) {
                    return true;
                }
                // A whole budget missed restarts from now; a late tick keeps the grid, so the rate holds.
                const auto budget = std::chrono::duration_cast<std::chrono::steady_clock::duration>(run.frame_budget);
                const auto base = now - run.deadline >= budget ? now : run.deadline;
                run.deadline = base + budget;
            }
            const step_result step = frame_step(disc, run);
            switch (step) {
            case step_result::idle:
                return true;
            case step_result::stop: {
                run_finish(disc, run);
                // Same order as the native driver: the dump after the shutdown hook, the channel put back last.
                const int dumped = params_.dump_frame_path.empty() ? RV_OK : run_dump_frame();
                run_release_channel();
                // A failed put-back outranks a refused dump, as in the native driver.
                const int result = console_must_stop_ ? RV_ERR_IO : dumped;
                if (result == RV_OK) {
                    RV_LOG_INFO("pconsole", "run ended");
                } else if (console_must_stop_) {
                    RV_LOG_ERR("pconsole", "run ended: the script state was not restored after a failed reload");
                } else {
                    RV_LOG_ERR("pconsole", "run ended: the last frame was not written to the dump");
                }
                // The loop ends: no next frame is asked.
                return false;
            }
            case step_result::frame:
                break;
            }
        }
        return true;
    });

    run_web_loop(body);
    // Not reached on web: the unwind above never returns.
    return RV_OK;
}

int64_t rv_pconsole::boot_screen_run(std::function<int64_t()> rest)
{
    if (!boot_screen_begin()) {
        return rest();
    }

    // Heap: as in disc_run, the body outlives the unwind and holds rest by value. due is the earliest next step:
    // the first step runs at once, and each later one waits the native slice after the previous one.
    auto *body = new std::function<bool()>([this, rest, due = std::chrono::steady_clock::now()]() mutable -> bool {
        const auto now = std::chrono::steady_clock::now();
        if (now < due) {
            return true;
        }
        due = now + RV_PCONSOLE_BOOT_SCREEN_SLICE;
        switch (boot_screen_step()) {
        case boot_screen_result::running:
            return true;
        case boot_screen_result::quit:
            RV_LOG_INFO("pconsole", "the disc does not start: the start screen was stopped");
            return false;
        case boot_screen_result::done:
            break;
        }
        // rest returns only when the disc did not start: a started disc unwinds out of this frame.
        const int64_t rc = rest();
        RV_LOG_ERR("pconsole", "the disc did not start (code {}); the cause is logged above or on stderr", rc);
        return false;
    });

    run_web_loop(body);
    // Not reached on web: the unwind above never returns.
    return RV_OK;
}

} // namespace rv_3dmppc
