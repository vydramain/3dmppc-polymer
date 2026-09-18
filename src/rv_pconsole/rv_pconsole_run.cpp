// The frame loop: inversion of control made concrete.
//
// The loop belongs to the console. A disc lives inside the rv_de hooks and
// never owns a loop of its own, which is what lets the console decide the one
// thing a game must not decide for itself - when a frame happens.
//
// Split out of rv_pconsole.cpp, and split again inside: the loop used to be one
// function of three hundred lines that pumped events, read the pause key,
// served the command channel, held a pause, ran the frame, fed the SPU,
// answered a step, watched for a broken script hook, polled the disc and then
// waited in one of three different ways. Every one of those is a step with a
// name, and a reader looking for the timing rule should not have to walk past
// the other nine to find it.
//
// What did NOT change in the split: the order. Frame N is exactly N/target_fps
// of machine time in every mode, the quit check runs before the pause so it can
// never be trapped behind one, and the step is answered only after its frame.
#include "rv_pconsole/rv_pconsole.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pcpause_overlay.hpp"

namespace rv_3dmppc
{
namespace
{

// PCM the audio output may hold ahead of the device, in frames of the
// timeline, ~33 ms at 60 fps. Absorbs a late frame without an audible gap and
// bounds voice_play-to-ear latency.
constexpr int64_t RV_PCONSOLE_AUDIO_QUEUE_TIMELINE_FRAMES = 2;

// A device that has taken nothing for this long stopped draining.
constexpr auto RV_PCONSOLE_AUDIO_STALL = std::chrono::milliseconds(250);

// How long a paused frame sleeps before looking again. Short enough that the
// window stays responsive and a command is acted on without a visible delay,
// long enough that a paused console is not a core running flat out.
constexpr auto RV_PCONSOLE_PAUSE_SLICE = std::chrono::milliseconds(2);

using rv_pcclock = std::chrono::steady_clock;

} // namespace

const char *rv_pconsole::pacing_name(pacing mode)
{
    switch (mode) {
        case pacing::none:
            return "nothing (fixed step)";
        case pacing::audio:
            return "audio output";
        case pacing::clock:
            return "steady clock";
    }
    return "?";
}

int64_t rv_pconsole::run_start(rv_de *disc, run_state &run)
{
    // Everything a disc needs must be ready before its code runs, so the
    // window is opened HERE, before disc_initialize() below - never after.
    // disc_title() is a plain accessor (pdk/de/rv_de.h) with no dependency
    // on disc_initialize() having run, so it is safe to call this early.
    //
    // A window that was not wanted (rv_pcplatform_wants::window == false)
    // opens nothing and answers RV_OK; a window that was wanted and refused
    // to come up is a WARNING, not a stop - the disc runs unpresented instead
    // of not running at all.
    const int64_t opened = platform_.window().open(
        disc->disc_title(disc->self), cv_->screen_width(), cv_->screen_height(), params_.scale);
    if (0 > opened) {
        RV_LOG_WARN("pconsole", "display did not come up for '{}', continuing without presentation",
            disc->disc_title(disc->self));
    }

    const int64_t dir = disc->disc_initialize(disc->self, reinterpret_cast<rv_pdko *>(this));
    if (0 > dir) {
        RV_LOG_ERR("pconsole",
            "Can't initialize mppcdisc in console. Please check mppcdisc consistency: {}", dir);
        return RV_ERR_INVAL;
    }

    // The disc has started, so it is now owed a disc_shutdown() (rv_de.hpp says
    // the hook never runs for a disc that refused to start). For a disc that came
    // off an archive that debt belongs to the loader, whose teardown chain runs it
    // before unmapping the code; telling it here is what separates "started" from
    // "loaded". A disc this loader did not produce is ignored - see run_finish.
    if (loader_ != nullptr) {
        loader_->notify_initialized(disc);
    }

    run.target_fps = params_.target_fps ? params_.target_fps : 60;
    run.frame_budget = std::chrono::duration<double>{ 1.0 / static_cast<double>(run.target_fps) };

    // One timeline. Frame N is exactly N/target_fps of machine time in every
    // mode: dt never varies with wall clock or with the audio device. The wall
    // clock and the audio device only decide WHEN the next tick runs, never how
    // long it is - a host too slow gets slow motion, never a jump, and -F is
    // reproducible frame for frame.
    run.dt = 1.0f / static_cast<float>(run.target_fps);

    // Not fixed for the run: a stall of the audio device switches this to clock
    // pacing mid-run (see run_pace).
    run.mode = params_.fixed_step
        ? pacing::none
        : (platform_.audio().available() ? pacing::audio : pacing::clock);
    run.queue_target = RV_PCONSOLE_AUDIO_QUEUE_TIMELINE_FRAMES *
        (RV_PCCA_PCM_RATE / static_cast<int64_t>(run.target_fps));
    run.deadline =
        rv_pcclock::now() + std::chrono::duration_cast<rv_pcclock::duration>(run.frame_budget);

    RV_LOG_INFO("pconsole", "running mppcdisc '{}' ({}, dt 1/{} s, paced by {})",
        disc->disc_title(disc->self),
        platform_.window().presenting() ? "presented" : "unpresented", run.target_fps,
        pacing_name(run.mode));

    // The channel opens AFTER disc_initialize: a run that refused to start must
    // not have put stdin into non-blocking mode and announced a protocol on
    // stdout.
    if (params_.dev) {
        // Belt and braces: rv_pboot already refuses --dev in a console built
        // without the development runtime, so the null factory is unreachable
        // from here - but an unchecked null would be a crash, not a refusal.
        dev_ = rv_pcdevchan_make();
        if (!dev_) {
            RV_LOG_ERR("pconsole",
                "development runtime unavailable: this console was built without it "
                "(-D3DMPPC_DEVTOOLS=ON)");
        } else {
            RV_LOG_INFO("pconsole", "development runtime armed");
        }
    }

    // Independent of the channel: --paused is about the loop, and the Pause key
    // can lift it with no channel at all. It takes effect before frame 0, which
    // means the disc's own boot hooks have run but nothing has been drawn - a
    // controllable first moment, not a debugger attached before initialisation.
    paused_ = params_.loop_paused;
    if (paused_) {
        RV_LOG_INFO("pconsole", "stopped before frame 0; lift it with the pause key{}",
            dev_ ? " or a resume/step request" : "");
    }

    frames_ = 0;
    return RV_OK;
}

void rv_pconsole::run_pause_key()
{
    // NOT under --dev: stopping the machine is an operator's act, the same
    // category as closing the window, and it is wanted in an ordinary run more
    // than in a development one. The key never reaches the disc - the platform
    // keeps it out of the keyboard snapshot rv_cio hands over - so a paused game
    // cannot see a phantom button, and this is a pause OF THE CONSOLE, not a
    // state inside the game.
    const uint32_t asked = platform_.window().consume_pause_requests();
    if (asked == 0) {
        return;
    }
    // An odd number of presses since the last look is a change of state; an even
    // number is a press and an unpress that both landed in one frame and cancel
    // out.
    if ((asked & 1u) == 0u) {
        return;
    }
    paused_ = !paused_;
    RV_LOG_INFO("pconsole", "{} by the pause key at frame {}", paused_ ? "stopped" : "running again",
        frames_);
    dev_note_pause();
}

bool rv_pconsole::run_dev_commands()
{
    if (!dev_) {
        return false;
    }
    // The frame boundary, and the only place a command is executed. Here no
    // script call is in flight and the lua stack is at its base, which is what
    // makes replacing code safe - the pause is for the developer's eyes, never
    // a precondition for the swap.
    dev_service();
    if (!quit_by_command_) {
        return false;
    }
    RV_LOG_INFO("pconsole", "shutdown requested over the development channel after {} frame(s)",
        frames_);
    return true;
}

bool rv_pconsole::run_hold_paused(run_state &run)
{
    // Stopped is stopped, whoever asked. No frame is created: no update, no
    // render, no advance, no counter. The window still gets its last picture and
    // its events, so it can be moved, paused again and closed while stopped -
    // and the quit check in the loop runs first every time, so Ctrl+C and the
    // close button are never trapped behind a pause.
    if (paused_ && step_reply_id_ < 0) {
        // Built once per pause, not once per slice: the picture cannot change
        // while no frame is running. A headless run builds nothing at all -
        // there would be nowhere to put it.
        if (platform_.window().presenting()) {
            if (!pause_overlay_valid_) {
                rv_pcpause_overlay_build(pause_overlay_, cv_->last_frame(), cv_->screen_width(),
                    cv_->screen_height());
                pause_overlay_valid_ = true;
            }
            platform_.window().present(pause_overlay_.data());
        }
        std::this_thread::sleep_for(RV_PCONSOLE_PAUSE_SLICE);
        run.left_pause = true;
        return true;
    }

    // A frame is about to run, so whatever is on screen is about to stop being
    // what a pause would show.
    pause_overlay_valid_ = false;
    if (run.left_pause) {
        // Re-base the clock. A deadline computed before the pause is now far in
        // the past, and run_pace would read that as "we are behind" and run a
        // burst of frames as fast as it could to catch up - a visible jump,
        // which is exactly what a pause must not cause.
        run.deadline =
            rv_pcclock::now() + std::chrono::duration_cast<rv_pcclock::duration>(run.frame_budget);
        run.left_pause = false;
    }
    return false;
}

void rv_pconsole::run_frame(rv_de *disc, run_state &run)
{
    disc->frame_update(disc->self, run.dt);
    // frame_render() is always called: with cv null the calls it makes land on
    // rv_pccv_null, which touches no rasterizer, no framebuffer and no virtual
    // VRAM. A run whose video merely failed to come up still renders every
    // frame - that run wanted a picture, it just has no screen to put it on.
    disc->frame_render(disc->self);

    if (const uint32_t *argb = cv_->last_frame()) {
        platform_.window().present(argb);
    }

    // Bresenham accumulator. N frames give exactly floor(N * rate / fps)
    // samples, with zero drift, because the remainder of every division is
    // carried forward instead of dropped.
    run.audio_phase += RV_PCCA_PCM_RATE;
    const int64_t samples = run.audio_phase / static_cast<int64_t>(run.target_fps);
    run.audio_phase %= static_cast<int64_t>(run.target_fps);
    ca_->advance(pcm_.data(), samples);

    if (run.mode != pacing::audio) {
        return;
    }
    run.audio_paced_ever = true;
    if (frames_ >= RV_PCONSOLE_AUDIO_QUEUE_TIMELINE_FRAMES &&
        platform_.audio().queued_frames() == 0) {
        ++run.audio_underruns;
    }
    platform_.audio().write(pcm_.data(), samples);
    run.audio_written += samples;
    run.audio_peak_queued = std::max(run.audio_peak_queued, platform_.audio().queued_frames());
}

void rv_pconsole::run_pace(run_state &run)
{
    if (run.mode == pacing::none) {
        // Unpaced: the loop runs as fast as it can, and the audio output was
        // never fed at all.
        return;
    }

    if (run.mode == pacing::clock) {
        const rv_pcclock::time_point after = rv_pcclock::now();
        if (after < run.deadline) {
            std::this_thread::sleep_until(run.deadline);
            run.deadline += std::chrono::duration_cast<rv_pcclock::duration>(run.frame_budget);
        } else {
            // Fell behind: re-base instead of letting missed deadlines pile up
            // into a burst of zero-length frames.
            run.deadline =
                after + std::chrono::duration_cast<rv_pcclock::duration>(run.frame_budget);
        }
        return;
    }

    // Paced by backpressure: wait until the device has taken enough that the
    // queue is back under its target.
    rv_pcclock::time_point stall_since = rv_pcclock::now();
    int64_t last_queued = platform_.audio().queued_frames();
    bool stalled = false;
    while (platform_.audio().queued_frames() > run.queue_target) {
        platform_.pump();
        if (platform_.quit_requested()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const int64_t queued_now = platform_.audio().queued_frames();
        if (queued_now < last_queued) {
            stall_since = rv_pcclock::now();
            last_queued = queued_now;
        } else if (rv_pcclock::now() - stall_since >= RV_PCONSOLE_AUDIO_STALL) {
            RV_LOG_WARN("pconsole",
                "audio output stopped draining, pacing by the steady clock from now on");
            stalled = true;
            break;
        }
    }
    if (stalled) {
        // Switch pacing for every following frame; the run itself keeps going -
        // only the wait ABOVE is abandoned. The next frame's quit_requested()
        // check is what ends the run if the stall was actually a closed window
        // or a signal.
        run.mode = pacing::clock;
        run.deadline =
            rv_pcclock::now() + std::chrono::duration_cast<rv_pcclock::duration>(run.frame_budget);
    }
}

void rv_pconsole::run_finish(rv_de *disc, const run_state &run)
{
    if (run.audio_paced_ever) {
        RV_LOG_INFO("pconsole",
            "audio output: {} frame(s) written, {} underrun(s), peak queue {} frame(s)",
            run.audio_written, run.audio_underruns, run.audio_peak_queued);
    }

    // The last hook, after the last frame. For a LOADED disc it is deliberately
    // NOT called here: it is the first link of the loader's teardown chain
    // (disc_shutdown -> destroy -> dlclose -> unlink), which exists as one
    // sequence precisely so it cannot be run out of order or twice. The built-in
    // rv_dmain has no loader behind it, so for that one the frame loop is the
    // only place the hook can come from.
    if (loader_ == nullptr || loader_->disc() != disc) {
        disc->disc_shutdown(disc->self);
    }

    // Devkit: hand the last frame the machine produced to disk, if asked. After
    // the loop rather than inside it, so a dump costs nothing per frame. The
    // pause overlay is deliberately not what lands here - it was composed into a
    // copy, so this is what the DISC drew.
    cv_->dump_last_frame(params_.dump_frame_path);

    // Best effort, bounded: the last answer should reach a client that is still
    // there, and a client that is gone must not hold the shutdown open.
    if (dev_) {
        dev_->drain(std::chrono::milliseconds(50));
    }
}

int64_t rv_pconsole::disc_run(rv_de *disc)
{
    run_state run;
    const int64_t started = run_start(disc, run);
    if (started < 0) {
        return started;
    }

    for (;;) {
        // One pump per frame turns the platform's event stream into the
        // instantaneous port snapshots rv_cio hands the disc. It must happen
        // before frame_update, or the disc reads input that is one frame stale.
        platform_.pump();

        // The power switch, and FIRST on purpose. A closed window or
        // SIGINT/SIGTERM is the console's own shutdown path - rv_de::
        // disc_release() is the disc ASKING to stop, and pulling the plug was
        // never the disc's decision. Ahead of the pause below so that a stopped
        // console is still killable the ordinary way.
        if (platform_.quit_requested()) {
            RV_LOG_INFO("pconsole",
                "shutdown requested (window closed or SIGINT/SIGTERM) after {} frame(s)", frames_);
            break;
        }

        run_pause_key();

        if (run_dev_commands()) {
            break;
        }

        if (run_hold_paused(run)) {
            continue;
        }

        run_frame(disc, run);
        dev_after_frame();

        // Polled every frame, per the contract. Checked after the frame so the
        // disc gets to draw the frame on which it decided to quit.
        if (disc->disc_release(disc->self)) {
            RV_LOG_INFO("pconsole", "mppcdisc released after {} frame(s)", frames_ + 1);
            break;
        }

        ++frames_;
        if (params_.max_frames && frames_ >= params_.max_frames) {
            RV_LOG_INFO("pconsole", "frame budget of {} reached", params_.max_frames);
            break;
        }

        run_pace(run);
    }

    run_finish(disc, run);
    return RV_OK;
}

} // namespace rv_3dmppc
