// Browser driver of the frame loop: the browser calls the frame from requestAnimationFrame, so nothing here sleeps.
// Pacing by the console's clock: frames follow the console's rate, not the monitor's 120/144 Hz refresh.
// simulate_infinite_loop never returns to disc_run, so the code after disc_run in rv_pboot.cpp is unreachable on web.
#include "rv_pconsole/rv_pconsole.hpp"

#include <chrono>

#include <emscripten/emscripten.h>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

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

    // Heap: simulate_infinite_loop leaves this frame without running destructors, and the callbacks
    // use the context for as long as the page lives.
    struct web_loop {
        rv_pconsole *console;
        rv_de *disc;
        run_state run;
    };
    auto *ctx = new web_loop{ this, disc, run };

    emscripten_set_main_loop_arg(
        [](void *arg) {
            auto *web = static_cast<web_loop *>(arg);
            rv_pconsole *console = web->console;
            for (int steps = 0; steps < RV_PCONSOLE_WEB_MAX_STEPS_PER_CALL; ++steps) {
                if (web->run.mode == pacing::clock) {
                    const auto now = std::chrono::steady_clock::now();
                    // Not yet due: the next callback looks again.
                    if (now < web->run.deadline) {
                        return;
                    }
                    // A whole budget missed restarts from now; a late tick keeps the grid, so the rate holds.
                    const auto budget = std::chrono::duration_cast<std::chrono::steady_clock::duration>(web->run.frame_budget);
                    const auto base = now - web->run.deadline >= budget ? now : web->run.deadline;
                    web->run.deadline = base + budget;
                }
                const step_result step = console->frame_step(web->disc, web->run);
                switch (step) {
                case step_result::idle:
                    return;
                case step_result::stop: {
                    console->run_finish(web->disc, web->run);
                    // Same order as the native driver: the dump after the shutdown hook, the channel put back last.
                    const int dumped = console->params_.dump_frame_path.empty() ? RV_OK : console->run_dump_frame();
                    console->run_release_channel();
                    // A failed put-back outranks a refused dump, as in the native driver.
                    const int result = console->console_must_stop_ ? RV_ERR_IO : dumped;
                    if (result == RV_OK) {
                        RV_LOG_INFO("pconsole", "run ended");
                    } else if (console->console_must_stop_) {
                        RV_LOG_ERR("pconsole", "run ended: the script state was not restored after a failed reload");
                    } else {
                        RV_LOG_ERR("pconsole", "run ended: the last frame was not written to the dump");
                    }
                    emscripten_cancel_main_loop();
                    return;
                }
                case step_result::frame:
                    break;
                }
            }
        },
        ctx,
        0,
        1);

    // Not reached on web: the main loop has been registered and the call above did not return.
    return RV_OK;
}

} // namespace rv_3dmppc
