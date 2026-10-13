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
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <thread>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pcpause_overlay.hpp"

namespace rv_3dmppc
{
namespace
{

using rv_pcclock = std::chrono::steady_clock;

// Longest single wait in run_pace. Before each slice run_pace tops up the audio
// device, on top of the top-up once per frame in frame_step. Price: about 1000
// wakeups per second while a frame waits; a shorter slice refills sooner and costs more CPU.
constexpr auto RV_PCONSOLE_PACE_SLICE = std::chrono::milliseconds(1);

// Belt and braces: a player build never declares the --dev field that could
// set this true (rv_pboot_args.hpp), so the null factory is unreachable from
// here in practice - but an unchecked null would be a crash, not a refusal.
std::unique_ptr<rv_pccmdchan> pconsole_make_dev_cmd()
{
    auto cmd = rv_pccmdchan_make();
    if (!cmd) {
        RV_LOG_ERR("pconsole",
            "development runtime unavailable: this console was built without it "
            "(-D3DMPPC_DEVTOOLS=ON)");
        return nullptr;
    }
    RV_LOG_INFO("pconsole", "development runtime armed");
    return cmd;
}

} // namespace

const char *rv_pconsole::pacing_name(pacing mode)
{
    switch (mode) {
    case pacing::none:
        return "nothing (fixed step)";
    case pacing::clock:
        return "steady clock";
    }
    return "?";
}

const char *rv_pconsole::sound_name(sound_out sound)
{
    switch (sound) {
    case sound_out::device:
        return "audio device";
    case sound_out::frames:
        return "frame steps";
    }
    return "?";
}

void rv_pconsole::audio_fill(void *context, int16_t *interleaved, int64_t frames)
{
    // The device's demand, not the frame's: the machine advances exactly what the
    // device asks for, so its sound stays in step with what the device plays.
    auto *console = static_cast<rv_pconsole *>(context);
    console->ca_->advance(interleaved, frames);
    console->audio_dump_write(interleaved, frames);
    console->audio_device_frames_ += frames;
}

// The dump is raw S16LE, written by copying memory: that is the file format only on a little-endian host.
static_assert(std::endian::native == std::endian::little, "--dump-audio writes S16LE by copying memory");

int rv_pconsole::audio_dump_open()
{
    audio_dump_frames_ = 0;
    audio_dump_failed_ = false;
    if (params_.dump_audio_path.empty()) {
        return RV_OK;
    }
    audio_dump_ = std::fopen(params_.dump_audio_path.c_str(), "wb");
    if (audio_dump_ == nullptr) {
        const int err = errno;
        RV_LOG_ERR("pconsole",
            "audio dump not written: cannot open '{}': {}",
            rv_pdklib::rv_log_escape(params_.dump_audio_path.c_str()),
            std::strerror(err));
        return RV_ERR_IO;
    }
    return RV_OK;
}

void rv_pconsole::audio_dump_write(const int16_t *interleaved, int64_t frames)
{
    if (audio_dump_ == nullptr) {
        return;
    }
    const size_t count = static_cast<size_t>(frames * RV_PCCA_PCM_CHANNELS);
    if (std::fwrite(interleaved, sizeof(int16_t), count, audio_dump_) != count) {
        const int err = errno;
        RV_LOG_ERR("pconsole", "audio dump stopped after {} stereo frame(s): {}", audio_dump_frames_, std::strerror(err));
        // The cause is logged already; the close adds nothing to report.
        std::fclose(audio_dump_);
        audio_dump_ = nullptr;
        audio_dump_failed_ = true;
        return;
    }
    audio_dump_frames_ += frames;
}

void rv_pconsole::audio_dump_close()
{
    if (audio_dump_ == nullptr) {
        return;
    }
    // fclose is where buffered bytes reach the disk, so its failure may be a lost tail.
    if (std::fclose(audio_dump_) != 0) {
        const int err = errno;
        RV_LOG_ERR("pconsole", "audio dump not completed, the end may be lost: {}", std::strerror(err));
        audio_dump_failed_ = true;
    }
    audio_dump_ = nullptr;
}

int64_t rv_pconsole::run_start(rv_de *disc, run_state &run)
{
    // The dump file opens before anything starts, so a refused path leaves nothing to undo.
    const int dump_opened = audio_dump_open();
    if (dump_opened != RV_OK) {
        return dump_opened;
    }

    // Everything a disc needs must be ready before its code runs, so the
    // window is opened HERE, before disc_initialize() below - never after.
    // disc_title() is a plain accessor (pdk/de/rv_de.h) with no dependency
    // on disc_initialize() having run, so it is safe to call this early.
    //
    // A window that was not wanted (rv_pcplatform_wants::window == false)
    // opens nothing and answers RV_OK; a window that was wanted and refused
    // to come up is a WARNING, not a stop - the disc runs unpresented instead
    // of not running at all.
    const int64_t opened =
        platform_.window().open(disc->disc_title(disc->self), cv_->screen_width(), cv_->screen_height(), params_.scale);
    if (0 > opened) {
        RV_LOG_WARN("pconsole",
            "display did not come up for '{}', continuing without presentation",
            disc->disc_title(disc->self));
    }

    const int64_t dir = disc->disc_initialize(disc->self, reinterpret_cast<rv_pdko *>(this));
    if (0 > dir) {
        RV_LOG_ERR("pconsole", "Can't initialize mppcdisc in console. Please check mppcdisc consistency: {}", dir);
        audio_dump_close();
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

    run.target_fps = params_.target_fps ? params_.target_fps : RV_PCONSOLE_DEFAULT_TARGET_FPS;
    run.frame_budget = std::chrono::duration<double>{ 1.0 / static_cast<double>(run.target_fps) };

    // One timeline. Frame N is exactly N/target_fps of machine time in every
    // mode: dt never varies with wall clock or with the audio device. The wall
    // clock alone decides WHEN the next tick runs, never how long it is - a host
    // too slow gets slow motion, never a jump, and -F is reproducible frame for frame.
    run.dt = 1.0f / static_cast<float>(run.target_fps);

    // Two decisions, each made once. The frame clock is never the audio device:
    // -F steps by count, every other run by the clock. The device pulls its sound
    // only when the run is not fixed-step and a device exists; otherwise the frame
    // loop advances the machine itself.
    run.mode = params_.fixed_step ? pacing::none : pacing::clock;
    run.sound = (!params_.fixed_step && platform_.audio().available()) ? sound_out::device : sound_out::frames;
    audio_device_frames_ = 0;
    run.deadline = rv_pcclock::now() + std::chrono::duration_cast<rv_pcclock::duration>(run.frame_budget);
    if (run.sound == sound_out::device) {
        platform_.audio().attach_source(&rv_pconsole::audio_fill, this);
    }

    RV_LOG_INFO("pconsole",
        "running mppcdisc '{}' ({}, dt 1/{} s, paced by {}, sound from {})",
        disc->disc_title(disc->self),
        platform_.window().presenting() ? "presented" : "unpresented",
        run.target_fps,
        pacing_name(run.mode),
        sound_name(run.sound));

    // The channel opens AFTER disc_initialize: a run that refused to start must
    // not have put stdin into non-blocking mode and announced a protocol on
    // stdout.
    if (params_.dev) {
        cmd_ = pconsole_make_dev_cmd();
    }

    // Independent of the channel: --paused is about the loop, and the Pause key
    // can lift it with no channel at all. It takes effect before frame 0, which
    // means the disc's own boot hooks have run but nothing has been drawn - a
    // controllable first moment, not a debugger attached before initialisation.
    paused_ = params_.loop_paused;
    if (paused_) {
        RV_LOG_INFO("pconsole",
            "stopped before frame 0; lift it with the pause key{}",
            cmd_ ? " or a resume/step request" : "");
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
    RV_LOG_INFO("pconsole", "{} by the pause key at frame {}", paused_ ? "stopped" : "running again", frames_);
    cmd_note_pause();
}

bool rv_pconsole::run_cmd_channel()
{
    // A pending step is answered after its own frame. Taking another request before
    // that frame would overwrite step_reply_id_ and lose the answer to the step.
    if (!cmd_ || step_reply_id_ >= 0) {
        return false;
    }
    // The frame boundary, and the only place a command is executed. Here no
    // script call is in flight and the lua stack is at its base, which is what
    // makes replacing code safe - the pause is for the developer's eyes, never
    // a precondition for the swap.
    cmd_service();
    if (!quit_by_command_) {
        return false;
    }
    RV_LOG_INFO("pconsole", "shutdown requested over the development channel after {} frame(s)", frames_);
    return true;
}

void rv_pconsole::run_show_pause()
{
    // Built once per pause, not once per slice: the picture cannot change
    // while no frame is running. A headless run builds nothing at all -
    // there would be nowhere to put it - and an embedded one keeps the
    // disc's last frame, since its host shows the pause.
    if (!platform_.window().presenting() || platform_.window().embedded()) {
        return;
    }
    if (!pause_overlay_valid_) {
        rv_pcpause_overlay_build(pause_overlay_, cv_->last_frame(), cv_->screen_width(), cv_->screen_height());
        pause_overlay_valid_ = true;
    }
    platform_.window().present(pause_overlay_.data());
}

bool rv_pconsole::run_hold_paused(run_state &run)
{
    // Stopped is stopped, whoever asked. No frame is created: no update, no
    // render, no advance, no counter. The window still gets its last picture and
    // its events, so it can be moved, paused again and closed while stopped -
    // and the quit check in the loop runs first every time, so Ctrl+C and the
    // close button are never trapped behind a pause.
    if (paused_ && step_reply_id_ < 0) {
        run_show_pause();
        // Only the first stopped pass holds the device: pcca does not advance while the
        // machine is stopped, so the device must not pull from it either.
        if (!run.left_pause && run.sound == sound_out::device && platform_.audio().pause() != RV_OK) {
            RV_LOG_WARN("pconsole", "audio device refused to pause, it may keep playing what it holds");
        }
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
        run.deadline = rv_pcclock::now() + std::chrono::duration_cast<rv_pcclock::duration>(run.frame_budget);
        // A step lifts the device for this frame's window, on the clock. The sound of a
        // step is what the device played and pulled in that window, not "this frame's"
        // sound; with -F (sound_out::frames) the sound goes frame by frame.
        if (run.sound == sound_out::device && platform_.audio().resume() != RV_OK) {
            RV_LOG_WARN("pconsole", "audio device refused to resume, the run goes on but its sound may stay paused");
        }
        run.left_pause = false;
    }
    return false;
}

void rv_pconsole::run_frame(rv_de *disc, run_state &run)
{
    // The camera and the triangle count of cg are valid within one frame only.
    cg_->frame_reset();
    disc->frame_update(disc->self, run.dt);
    // frame_render() is always called: with cv null the calls it makes land on
    // rv_pccv_null, which touches no rasterizer, no framebuffer and no virtual
    // VRAM. A run whose video merely failed to come up still renders every
    // frame - that run wanted a picture, it just has no screen to put it on.
    disc->frame_render(disc->self);

    if (const uint32_t *argb = cv_->last_frame()) {
        platform_.window().present(argb);
    }

    // With a device the sound is pulled in audio_fill, not stepped here.
    if (run.sound != sound_out::frames) {
        return;
    }
    // Bresenham accumulator. N frames give exactly floor(N * rate / fps)
    // samples, with zero drift, because the remainder of every division is
    // carried forward instead of dropped.
    run.audio_phase += RV_PCCA_PCM_RATE;
    const int64_t samples = run.audio_phase / static_cast<int64_t>(run.target_fps);
    run.audio_phase %= static_cast<int64_t>(run.target_fps);
    ca_->advance(pcm_.data(), samples);
    audio_dump_write(pcm_.data(), samples);
}

void rv_pconsole::run_pace(run_state &run)
{
    // Unpaced runs never wait.
    if (run.mode != pacing::clock) {
        return;
    }

    const rv_pcclock::time_point start = rv_pcclock::now();
    const bool behind = start >= run.deadline;
    // Wait in slices; the last one stops at the deadline, never past it.
    for (rv_pcclock::time_point now = start; !behind && now < run.deadline; now = rv_pcclock::now()) {
        platform_.audio().service();
        std::this_thread::sleep_until(std::min(now + RV_PCONSOLE_PACE_SLICE, run.deadline));
    }
    // Fell behind: re-base instead of letting missed deadlines pile up
    // into a burst of zero-length frames.
    const auto base = behind ? start : run.deadline;
    run.deadline = base + std::chrono::duration_cast<rv_pcclock::duration>(run.frame_budget);
}

void rv_pconsole::run_finish(rv_de *disc, const run_state &run)
{
    // Before the last hook: no device pull may reach a disc that is already shut down.
    platform_.audio().attach_source(nullptr, nullptr);
    if (run.sound == sound_out::device) {
        RV_LOG_INFO("pconsole", "audio output: {} frame(s) handed to the device", audio_device_frames_);
    }
    if (!params_.dump_audio_path.empty()) {
        audio_dump_close();
        // A failed dump has already logged its cause and the frames it kept; "written" would read as success.
        if (!audio_dump_failed_) {
            RV_LOG_INFO("pconsole",
                "audio dump: {} stereo frame(s) written to '{}'",
                audio_dump_frames_,
                rv_pdklib::rv_log_escape(params_.dump_audio_path.c_str()));
        }
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
}

int rv_pconsole::run_dump_frame()
{
    // Devkit: hand the last frame the machine produced to disk. The pause overlay
    // is never what lands here - it was composed into a copy, so this is what the
    // DISC drew.
    std::string dump_error;
    const int dumped = cv_->dump_last_frame(params_.dump_frame_path, dump_error);
    if (dumped != RV_OK) {
        RV_LOG_ERR("pconsole", "frame not written: {}", dump_error);
        return dumped;
    }
    return RV_OK;
}

void rv_pconsole::run_release_channel()
{
    // Best effort, bounded: the last answer should reach a client that is still
    // there, and a client that is gone must not hold the shutdown open.
    if (cmd_) {
        cmd_->drain(RV_PCONSOLE_SHUTDOWN_DRAIN_TIMEOUT);
    }
}

} // namespace rv_3dmppc
