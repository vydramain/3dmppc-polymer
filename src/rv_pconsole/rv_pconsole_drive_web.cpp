// Browser driver of the frame loop: a JS loop on requestAnimationFrame calls the frame export, so nothing here sleeps.
// Pacing by the console's clock: frames follow the console's rate, not the monitor's 120/144 Hz refresh.
// The unwind in disc_run never returns, so the code after disc_run in rv_pboot.cpp is unreachable on web.
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

    rv_pconsole_web_start_frames(body);
    emscripten_unwind_to_js_event_loop();
    // Not reached on web: the unwind above never returns to disc_run.
    return RV_OK;
}

// The web build runs rest at once: a wait here would freeze the page, which cannot repaint until the call returns.
int64_t rv_pconsole::boot_screen_run(std::function<int64_t()> rest)
{
    return rest();
}

} // namespace rv_3dmppc
