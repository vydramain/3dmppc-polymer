// Native driver of the frame loop: calls frame_step and sleeps only on idle iterations (paused, waiting for audio).
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

// How long disc_run sleeps on an idle iteration while waiting for the audio device
// to drain. Those idle iterations also serve the pause key and the devtools channel
// (a non-blocking read), so this is their polling rate: brief enough to stay
// responsive, long enough not to spin a core.
constexpr auto RV_PCONSOLE_AUDIO_POLL_INTERVAL = std::chrono::milliseconds(1);

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
        // The step never sleeps; this driver sleeps once per idle iteration, so a pause or an audio wait does not spin a core.
        switch (step) {
        case step_result::idle: {
            // Same condition as run_hold_paused's pause branch; a pending step makes it false.
            const bool stopped = paused_ && step_reply_id_ < 0;
            const auto nap = stopped ? RV_PCONSOLE_PAUSE_SLICE : RV_PCONSOLE_AUDIO_POLL_INTERVAL;
            std::this_thread::sleep_for(nap);
            break;
        }
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
    return RV_OK;
}

} // namespace rv_3dmppc
