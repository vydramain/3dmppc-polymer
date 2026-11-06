// Native driver of the frame loop: calls frame_step and sleeps only on idle iterations (paused).
// The pacing between frames is run_pace's job, not this driver's.
#include "rv_pconsole/rv_pconsole.hpp"

#include <chrono>
#include <thread>

#include "pdk/rv_err.h"

namespace rv_3dmppc
{
namespace
{

// How long disc_run sleeps on an idle iteration while stopped with no step pending
// (the condition of run_hold_paused's pause branch). Short enough that the window
// stays responsive and a command is acted on without a visible delay, long enough
// that a paused console is not a core running flat out.
constexpr auto RV_PCONSOLE_PAUSE_SLICE = std::chrono::milliseconds(2);

// One frame at 60 Hz: a key or button is answered within a frame, and a screen redrawn
// every pass does not keep a core busy.
constexpr auto RV_PCONSOLE_BOOT_SCREEN_SLICE = std::chrono::milliseconds(16);

} // namespace

int64_t rv_pconsole::disc_run(rv_de *disc)
{
    run_state run;
    const int64_t started = run_start(disc, run);
    if (started < 0) {
        return started;
    }

    for (;;) {
        const step_result step = frame_step(disc, run);
        if (step == step_result::stop) {
            break;
        }
        // The step never sleeps; this driver sleeps once per idle iteration, so a pause does not spin a core.
        switch (step) {
        case step_result::idle:
            std::this_thread::sleep_for(RV_PCONSOLE_PAUSE_SLICE);
            break;
        case step_result::frame:
            run_pace(run);
            break;
        case step_result::stop:
            break;
        }
    }

    run_finish(disc, run);
    // The dump is asked after the shutdown hook, the answer after the dump; a
    // refused dump is returned only once the answer has gone out.
    const int dumped = params_.dump_frame_path.empty() ? RV_OK : run_dump_frame();
    run_release_channel();
    // A failed put-back outranks a refused dump: the state is the bigger problem.
    if (console_must_stop_) {
        return RV_ERR_IO;
    }
    if (dumped != RV_OK) {
        return dumped;
    }
    if (audio_dump_failed_) {
        return RV_ERR_IO;
    }
    return RV_OK;
}

int64_t rv_pconsole::boot_screen_run(const std::function<int64_t()> &rest)
{
    if (!boot_screen_begin()) {
        return rest();
    }

    for (;;) {
        switch (boot_screen_step()) {
        case boot_screen_result::running:
            std::this_thread::sleep_for(RV_PCONSOLE_BOOT_SCREEN_SLICE);
            break;
        case boot_screen_result::quit:
            return RV_OK;
        case boot_screen_result::done:
            return rest();
        }
    }
}

} // namespace rv_3dmppc
