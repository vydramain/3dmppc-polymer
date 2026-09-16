#include "rv_pconsole.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "pdk/cl/rv_cl.h"
#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_font/rv_font_data.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/rv_pcslots.hpp"

// The console's PCM format and the platform's are the same format by
// construction (both are fixed, not negotiated), but they are declared in
// two different files for two different reasons, so a silent drift between
// them must fail the build rather than the ear.
static_assert(rv_3dmppc::RV_PCCA_PCM_RATE == rv_3dmppc::RV_PCPLATFORM_PCM_RATE,
    "the SPU's sample rate and the platform's PCM sink must agree");
static_assert(rv_3dmppc::RV_PCCA_PCM_CHANNELS == rv_3dmppc::RV_PCPLATFORM_PCM_CHANNELS,
    "the mixer's channel count and the platform's PCM sink must agree");

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

// How the next frame's start time is decided. The frame's DURATION is always
// 1/target_fps (see rv_pconsole_params::target_fps); pacing only decides WHEN
// that frame runs.
enum class rv_pcpacing { none, audio, clock };

const char *rv_pcpacing_name(rv_pcpacing pacing)
{
    switch (pacing) {
        case rv_pcpacing::none:
            return "nothing (fixed step)";
        case rv_pcpacing::audio:
            return "audio output";
        case rv_pcpacing::clock:
            return "steady clock";
    }
    return "?";
}

using rv_pcclock = std::chrono::steady_clock;

// What a stopped machine says. The console's own words, not the disc's - which
// is why it is spelled CONSOLE and not GAME: what stopped is the frame loop,
// and the disc is not paused so much as simply not being called.
constexpr std::string_view RV_PCONSOLE_PAUSE_LABEL = "CONSOLE PAUSED";
constexpr int RV_PCONSOLE_PAUSE_LABEL_SCALE = 2;

// One line of pdklib's bitmap font, straight into an ARGB buffer.
//
// Only the DATA is borrowed from pdklib, not its text drawer: that one paints
// through rv_cv primitives and needs the font uploaded into virtual VRAM, which
// is a disc's job and would mean the console allocating video memory behind the
// disc's back to print one word. Here the glyphs are what they are on the page -
// eight row bytes, top row first, high bit leftmost - and the destination is the
// host's pixel buffer, so a blit is the whole of it.
void rv_pcblit_text(uint32_t *dst, int64_t width, int64_t height, int64_t x0, int64_t y0,
    std::string_view text, int scale, uint32_t argb)
{
    int64_t pen = x0;
    for (const char c : text) {
        const int glyph = (c >= 32 && c <= 126) ? c - 32 : rv_pdklib::rv_font_notdef_index;
        const uint8_t *rows = &rv_pdklib::rv_font_bits[glyph * rv_pdklib::rv_font_cell_height];
        for (int row = 0; row < rv_pdklib::rv_font_cell_height; ++row) {
            for (int column = 0; column < rv_pdklib::rv_font_ink_width; ++column) {
                if ((rows[row] & (0x80u >> column)) == 0) {
                    continue;
                }
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        const int64_t px = pen + column * scale + sx;
                        const int64_t py = y0 + row * scale + sy;
                        // Clipped rather than assumed to fit: the label is
                        // sized for 320x240 and a disc may declare a smaller
                        // screen.
                        if (px >= 0 && px < width && py >= 0 && py < height) {
                            dst[py * width + px] = argb;
                        }
                    }
                }
            }
        }
        pen += rv_pdklib::rv_font_cell_width * scale;
    }
}

// The picture a stopped console presents: the last frame, dimmed, with the
// label across the middle.
//
// A COPY, never the disc's own framebuffer. Drawing into that would make the
// pause destructive - the text would end up in --dump-frame, and a second pause
// would print over the first - and the disc's last frame has to stay exactly
// what the disc drew.
void rv_pcbuild_pause_overlay(std::vector<uint32_t> &out, const uint32_t *frame,
    int64_t width, int64_t height)
{
    const std::size_t pixels = static_cast<std::size_t>(width * height);
    out.assign(pixels, 0xFF000000u);
    if (frame != nullptr) {
        for (std::size_t i = 0; i < pixels; ++i) {
            // Halved, not blacked out. The developer still needs to see WHAT is
            // on screen when they stopped it; the dimming is what keeps white
            // text readable over a bright frame.
            out[i] = 0xFF000000u | ((frame[i] >> 1) & 0x007F7F7Fu);
        }
    }

    // The font's three trailing columns are letter spacing, so the last cell
    // carries blank width that must come off before centring - otherwise the
    // line sits a few pixels left of centre.
    const int scale = RV_PCONSOLE_PAUSE_LABEL_SCALE;
    const int64_t text_width =
        static_cast<int64_t>(RV_PCONSOLE_PAUSE_LABEL.size()) * rv_pdklib::rv_font_cell_width * scale -
        (rv_pdklib::rv_font_cell_width - rv_pdklib::rv_font_ink_width) * scale;
    const int64_t text_height = rv_pdklib::rv_font_ink_height * scale;
    rv_pcblit_text(out.data(), width, height, (width - text_width) / 2,
        (height - text_height) / 2, RV_PCONSOLE_PAUSE_LABEL, scale, 0xFFFFFFFFu);
}

} // namespace
} // namespace rv_3dmppc

rv_3dmppc::rv_pconsole::rv_pconsole(const rv_3dmppc::rv_pconsole_conf &conf,
    rv_3dmppc::rv_pcplatform &platform, rv_3dmppc::rv_pcloader *loader)
    : params_(conf.params)
    , platform_(platform)
    , ca_(rv_pcca_make(conf.slots.ca, conf.ca))
    , cd_(rv_pccd_make(conf.slots.cd, conf.cd))
    , cio_(rv_pccio_make(conf.slots.cio, conf.cio, platform_))
    , cm_(rv_pccm_make(conf.slots.cm, conf.cm))
    , cv_(rv_pccv_make(conf.slots.cv, conf.cv))
    , cl_(rv_pccl_make(conf.slots.cl, conf.cl, *cd_))
    , loader_(loader)
    , pcm_(static_cast<size_t>(
          (RV_PCCA_PCM_RATE / static_cast<int64_t>(params_.target_fps ? params_.target_fps : 60) + 1) *
          RV_PCCA_PCM_CHANNELS))
    , script_entry_(conf.cl.script_entry)
{
}

// This is where the contract meets the machine. reinterpret_cast is mandatory
// here, not a style choice: rv_ca/rv_cv/... are incomplete to C++, so
// static_cast from or to them cannot compile. Every line hands out the slot's
// BASE address (ca_.get(), cd_.get(), ...); the extern "C" block in each
// XX/rv_pcXX.cpp casts back to that same base (one of several such cast
// sites, not the only one - see the block below).
rv_ca *rv_3dmppc::rv_pconsole::ca()
{
    return reinterpret_cast<rv_ca *>(ca_.get());
}
rv_cd *rv_3dmppc::rv_pconsole::cd()
{
    return reinterpret_cast<rv_cd *>(cd_.get());
}
rv_cm *rv_3dmppc::rv_pconsole::cm()
{
    return reinterpret_cast<rv_cm *>(cm_.get());
}
rv_cio *rv_3dmppc::rv_pconsole::cio()
{
    return reinterpret_cast<rv_cio *>(cio_.get());
}
rv_cv *rv_3dmppc::rv_pconsole::cv()
{
    return reinterpret_cast<rv_cv *>(cv_.get());
}
rv_cl *rv_3dmppc::rv_pconsole::cl()
{
    return reinterpret_cast<rv_cl *>(cl_.get());
}

bool rv_3dmppc::rv_pconsole::ready() const
{
    return ca_->valid() && cd_->valid() && cio_->valid() && cm_->valid() && cv_->valid() && cl_->valid();
}

int64_t rv_3dmppc::rv_pconsole::disc_run(rv_de *disc)
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
        RV_LOG_WARN("pconsole",
            "display did not come up for '{}', continuing without presentation",
            disc->disc_title(disc->self));
    }

    int64_t dir = disc->disc_initialize(disc->self, reinterpret_cast<rv_pdko *>(this));
    if (0 > dir) {
        RV_LOG_ERR("pconsole",
            "Can't initialize mppcdisc in console. Please check mppcdisc consistency: {}",
            dir);
        return RV_ERR_INVAL;
    }

    // The disc has started, so it is now owed a disc_shutdown() (rv_de.hpp says
    // the hook never runs for a disc that refused to start). For a disc that came
    // off an archive that debt belongs to the loader, whose teardown chain runs it
    // before unmapping the code; telling it here is what separates "started" from
    // "loaded". A disc this loader did not produce is ignored - see below.
    if (loader_ != nullptr) {
        loader_->notify_initialized(disc);
    }

    const uint64_t target_fps = params_.target_fps ? params_.target_fps : 60;
    const std::chrono::duration<double> frame_budget{ 1.0 / static_cast<double>(target_fps) };

    // one timeline. Frame N is exactly N/target_fps of machine time
    // in every mode: dt never varies with wall clock or with the audio
    // device. The wall clock and the audio device only decide WHEN the next
    // tick runs, never how long it is - a host too slow gets slow motion,
    // never a jump, and -F is reproducible frame for frame.
    const float dt = 1.0f / static_cast<float>(target_fps);

    // Not const: a stall of the audio device switches this to clock pacing
    // mid-run (see the stall handling below).
    rv_pcpacing pacing =
        params_.fixed_step ? rv_pcpacing::none : (platform_.audio().available() ? rv_pcpacing::audio : rv_pcpacing::clock);
    const int64_t queue_target =
        RV_PCONSOLE_AUDIO_QUEUE_TIMELINE_FRAMES * (RV_PCCA_PCM_RATE / static_cast<int64_t>(target_fps));

    RV_LOG_INFO("pconsole", "running mppcdisc '{}' ({}, dt 1/{} s, paced by {})", disc->disc_title(disc->self),
        platform_.window().presenting() ? "presented" : "unpresented", target_fps, rv_pcpacing_name(pacing));

    // The channel opens AFTER disc_initialize: a run that refused to start must
    // not have put stdin into non-blocking mode and announced a protocol on
    // stdout. --paused takes effect just below, which means frame 0 has not run
    // yet but the disc's own boot hooks have - it is a controllable first
    // moment, not a debugger attached before initialisation.
    if (params_.dev) {
        dev_.emplace();
        RV_LOG_INFO("pconsole", "development runtime armed");
    }
    // Independent of the channel: --paused is about the loop, and the Pause key
    // can lift it with no channel at all.
    paused_ = params_.loop_paused;
    if (paused_) {
        RV_LOG_INFO("pconsole", "stopped before frame 0; lift it with the pause key{}",
            dev_ ? " or a resume/step request" : "");
    }

    uint64_t frames = 0;
    bool left_pause = false;
    int64_t audio_phase = 0;
    int64_t audio_frames_written = 0;
    int64_t audio_underruns = 0;
    int64_t audio_peak_queued = 0;
    bool audio_pacing_ever = false;
    rv_pcclock::time_point t_deadline =
        rv_pcclock::now() + std::chrono::duration_cast<rv_pcclock::duration>(frame_budget);

    for (;;) {
        // One pump per frame turns the platform's event stream into the
        // instantaneous port snapshots rv_cio hands the disc. It must happen
        // before frame_update, or the disc reads input that is one frame
        // stale.
        platform_.pump();

        // The power switch. A closed window or SIGINT/SIGTERM is the
        // console's own shutdown path - rv_de::disc_release() is the disc
        // ASKING to stop, and pulling the plug was never the disc's decision.
        if (platform_.quit_requested()) {
            RV_LOG_INFO("pconsole", "shutdown requested (window closed or SIGINT/SIGTERM) after {} frame(s)",
                frames);
            break;
        }

        // The pause key. This is NOT under --dev: stopping the machine is an
        // operator's act, the same category as closing the window, and it is
        // wanted in an ordinary run more than in a development one. The key
        // never reaches the disc - the platform keeps it out of the keyboard
        // snapshot rv_cio hands over - so a paused game cannot see a phantom
        // button, and this is a pause OF THE CONSOLE, not a state inside the
        // game.
        if (const uint32_t asked = platform_.window().consume_pause_requests(); asked != 0) {
            // An odd number of presses since the last look is a change of
            // state; an even number is a press and an unpress that both landed
            // in one frame and cancel out.
            if ((asked & 1u) != 0u) {
                paused_ = !paused_;
                RV_LOG_INFO("pconsole", "{} by the pause key at frame {}",
                    paused_ ? "stopped" : "running again", frames);
                if (dev_) {
                    // The client did not ask for this, so it arrives as an
                    // event: something else moved the machine it is driving.
                    dev_->reply(std::format("0 event=pause mode={} frame={}",
                        paused_ ? "paused" : "running", frames));
                }
            }
        }

        // The frame boundary, and the only place a command is executed. Here no
        // script call is in flight and the lua stack is at its base, which is
        // what makes replacing code safe - the pause is for the developer's
        // eyes, never a precondition for the swap.
        if (dev_) {
            frames_ = frames;
            dev_service();
            if (quit_by_command_) {
                RV_LOG_INFO("pconsole", "shutdown requested over the development channel after {} frame(s)",
                    frames);
                break;
            }
        }

        // Stopped is stopped, whoever asked. No frame is created: no update, no
        // render, no advance, no counter. The window still gets its last
        // picture and its events, so it can be moved, paused again and closed
        // while stopped - and the quit check above runs first every time, so
        // Ctrl+C and the close button are never trapped behind a pause.
        if (paused_ && step_reply_id_ < 0) {
            // Built once per pause, not once per 2 ms: the picture cannot change
            // while no frame is running. A headless run builds nothing at all -
            // there would be nowhere to put it.
            if (platform_.window().presenting()) {
                if (!pause_overlay_valid_) {
                    rv_pcbuild_pause_overlay(pause_overlay_, cv_->last_frame(),
                        cv_->screen_width(), cv_->screen_height());
                    pause_overlay_valid_ = true;
                }
                platform_.window().present(pause_overlay_.data());
            }
            std::this_thread::sleep_for(RV_PCONSOLE_PAUSE_SLICE);
            left_pause = true;
            continue;
        }
        // A frame is about to run, so whatever is on screen is about to stop
        // being what a pause would show.
        pause_overlay_valid_ = false;
        if (left_pause) {
            // Re-base the clock. A deadline computed before the pause is now
            // far in the past, and the clock-paced branch below would read that
            // as "we are behind" and run a burst of frames as fast as it could
            // to catch up - a visible jump, which is exactly what a pause must
            // not cause.
            t_deadline = rv_pcclock::now() +
                std::chrono::duration_cast<rv_pcclock::duration>(frame_budget);
            left_pause = false;
        }

        disc->frame_update(disc->self, dt);
        // frame_render() is always called: with cv null the calls it makes
        // land on rv_pccv_null, which touches no rasterizer, no framebuffer
        // and no virtual VRAM. A run whose video merely failed to come up
        // still renders every frame - that run wanted a picture, it just has
        // no screen to put it on.
        disc->frame_render(disc->self);

        if (const uint32_t *argb = cv_->last_frame()) {
            platform_.window().present(argb);
        }

        // Bresenham accumulator. N frames give exactly
        // floor(N * rate / fps) samples, with zero drift, because the
        // remainder of every division is carried forward instead of dropped.
        audio_phase += RV_PCCA_PCM_RATE;
        const int64_t samples = audio_phase / static_cast<int64_t>(target_fps);
        audio_phase %= static_cast<int64_t>(target_fps);
        ca_->advance(pcm_.data(), samples);

        if (pacing == rv_pcpacing::audio) {
            audio_pacing_ever = true;
            if (frames >= RV_PCONSOLE_AUDIO_QUEUE_TIMELINE_FRAMES &&
                platform_.audio().queued_frames() == 0) {
                ++audio_underruns;
            }
            platform_.audio().write(pcm_.data(), samples);
            audio_frames_written += samples;
            audio_peak_queued = std::max(audio_peak_queued, platform_.audio().queued_frames());
        }

        // The step's answer, now that its frame is over. `step` means "one
        // frame has happened", so answering when the request arrived would be
        // answering for work not yet done.
        if (dev_ && step_reply_id_ >= 0) {
            dev_->reply(std::format("{} ok completed=1 frame={} mode=paused", step_reply_id_, frames + 1));
            step_reply_id_ = -1;
        }

        // Did a game hook fail this frame? rv_pccl counts every failed call, so
        // comparing that count is how the console finds out without the disc
        // having to tell it and without a contract change. In a development run
        // the machine stops there: a frozen picture with no explanation is the
        // worst possible answer, and the developer needs the state as it was
        // when it broke. id 0 marks a line nobody asked for.
        if (dev_ && cl_->valid()) {
            rv_pccl_status script;
            cl_->script_status(script);
            if (script.error_seq != dev_error_seq_) {
                dev_error_seq_ = script.error_seq;
                paused_ = true;
                dev_->reply(std::format("0 event=script_error frame={} msg={}", frames + 1,
                    rv_pcdev_hex(script.error)));
            }
        }

        // Polled every frame, per the contract. Checked after the frame so the
        // disc gets to draw the frame on which it decided to quit.
        if (disc->disc_release(disc->self)) {
            RV_LOG_INFO("pconsole", "mppcdisc released after {} frame(s)", frames + 1);
            break;
        }

        ++frames;
        if (params_.max_frames && frames >= params_.max_frames) {
            RV_LOG_INFO("pconsole", "frame budget of {} reached", params_.max_frames);
            break;
        }

        if (pacing == rv_pcpacing::none) {
            // Unpaced: the loop runs as fast as it can, and the audio output
            // was never fed above.
        } else if (pacing == rv_pcpacing::audio) {
            rv_pcclock::time_point stall_since = rv_pcclock::now();
            int64_t last_queued = platform_.audio().queued_frames();
            bool stalled = false;
            while (platform_.audio().queued_frames() > queue_target) {
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
                // Switch pacing for every following frame; the run itself
                // keeps going - only the wait ABOVE is abandoned. The next
                // frame's quit_requested() check is what ends the run if the
                // stall was actually a closed window or a signal.
                pacing = rv_pcpacing::clock;
                t_deadline = rv_pcclock::now() + std::chrono::duration_cast<rv_pcclock::duration>(frame_budget);
            }
        } else {
            const rv_pcclock::time_point after = rv_pcclock::now();
            if (after < t_deadline) {
                std::this_thread::sleep_until(t_deadline);
                t_deadline += std::chrono::duration_cast<rv_pcclock::duration>(frame_budget);
            } else {
                // Fell behind: re-base instead of letting missed deadlines pile
                // up into a burst of zero-length frames.
                t_deadline = after + std::chrono::duration_cast<rv_pcclock::duration>(frame_budget);
            }
        }
    }

    if (audio_pacing_ever) {
        RV_LOG_INFO("pconsole", "audio output: {} frame(s) written, {} underrun(s), peak queue {} frame(s)",
            audio_frames_written, audio_underruns, audio_peak_queued);
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
    // the loop rather than inside it, so a dump costs nothing per frame.
    cv_->dump_last_frame(params_.dump_frame_path);

    // Best effort, bounded: the last answer should reach a client that is still
    // there, and a client that is gone must not hold the shutdown open.
    if (dev_) {
        dev_->drain(std::chrono::milliseconds(50));
    }

    return RV_OK;
}


// --- the development runtime -------------------------------------------------
//
// One answer per request, in request order, on stdout. The shape is
// deliberately not JSON: every value that could carry a space, a newline or a
// NUL travels as hex, and once that is true there is nothing left to escape -
// so the protocol needs no serialiser, and the console needs no dependency to
// speak it.

namespace
{

std::string rv_pcdev_err(int64_t id, const char *token, int64_t rc, bool effects,
    std::string_view message)
{
    return std::format("{} err error={} rv_err={} effects={} msg={}", id, token, rc, effects ? 1 : 0,
        rv_3dmppc::rv_pcdev_hex(message));
}

} // namespace

void rv_3dmppc::rv_pconsole::dev_service()
{
    if (!dev_->connected()) {
        // Said once. The run carries on, and the pause state is deliberately
        // NOT touched: resuming here would restart a game the developer
        // stopped on purpose, at the moment they are least able to see why.
        if (!dev_close_logged_) {
            dev_close_logged_ = true;
            RV_LOG_WARN("pconsole",
                "development channel closed ({}); the run continues and the pause state is left as it is",
                dev_->closed_reason());
        }
        return;
    }

    rv_pcdevreq req;
    for (int64_t taken = 0; taken < RV_PCDEVCHAN_REQS_PER_TICK; ++taken) {
        if (!dev_->next_request(req)) {
            break;
        }
        dev_dispatch(req);
        // A step SUSPENDS the queue. Draining on would collapse three step
        // requests into one frame, and would answer a status for a frame that
        // has not happened yet.
        if (step_reply_id_ >= 0 || quit_by_command_) {
            break;
        }
    }
}

void rv_3dmppc::rv_pconsole::dev_dispatch(const rv_pcdevreq &req)
{
    const std::string_view verb = req.verb();

    if (verb == "status") {
        dev_status(req.id);
        return;
    }
    if (verb == "pause") {
        // Answered only now, which is after the decision and before the frame
        // that will not run: "ok" means no further frame happens until resume
        // or step.
        paused_ = true;
        dev_->reply(std::format("{} ok mode=paused frame={}", req.id, frames_));
        return;
    }
    if (verb == "resume") {
        paused_ = false;
        dev_->reply(std::format("{} ok mode=running frame={}", req.id, frames_));
        return;
    }
    if (verb == "step") {
        // No answer here: the frame has not run. disc_run answers once it has.
        paused_ = true;
        step_reply_id_ = req.id;
        return;
    }
    if (verb == "quit") {
        // The ORDINARY way out. Answering first and breaking the loop after
        // means the run leaves by the same path a closed window takes, so
        // disc_shutdown, the loader teardown and the frame dump all still
        // happen - there is no second shutdown path to keep in step.
        quit_by_command_ = true;
        dev_->reply(std::format("{} ok mode=stopped frame={}", req.id, frames_));
        return;
    }
    if (verb == "gc") {
        int64_t used = 0;
        const int64_t rc = cl_->state_collect(&used);
        if (rc < 0) {
            dev_->reply(rv_pcdev_err(req.id, "no_machine", rc, false, "this disc declared no lua machine"));
            return;
        }
        rv_pccl_status script;
        cl_->script_status(script);
        dev_->reply(std::format("{} ok lua_used={} lua_budget={} chunks={}", req.id, used,
            script.budget, script.slots));
        return;
    }
    if (verb == "reload") {
        dev_reload(req);
        return;
    }
    if (verb == "get") {
        dev_get(req);
        return;
    }
    if (verb == "asset") {
        dev_asset(req);
        return;
    }

    dev_->reply(rv_pcdev_err(req.id, "protocol", RV_ERR_INVAL, false,
        "unknown request; this console speaks status pause resume step reload asset get gc quit"));
}

void rv_3dmppc::rv_pconsole::dev_status(int64_t id)
{
    rv_pccl_status script;
    cl_->script_status(script);

    // `disc` identifies what is LOADED, not what is on disk: the manifest and
    // the budget were consumed at construction and this machine is built from
    // them, so a rebuild on disk changes nothing here. Comparing the two is the
    // client's job, and a difference means restart the process - native code
    // cannot be swapped under a live disc.
    const std::string disc_id = loader_ != nullptr ? loader_->info().disc_id : std::string("builtin");
    // The checksum of the disc.so this process actually mapped. `entry_hash`
    // next to it is the LUA half - the bytes the entry chunk is running. Two
    // hashes because there are two kinds of code, and only one of them can be
    // replaced without a restart; one number answering both questions would
    // answer neither.
    const std::string code_hash =
        loader_ != nullptr && !loader_->code_hash().empty() ? loader_->code_hash() : std::string("none");

    dev_->reply(std::format(
        "{} ok protocol=1 frame={} mode={} medium={} disc={} disc_hash={} pdk={}.{} "
        "entry_reloadable={} entry_revision={} entry_hash={:016x} lua_used={} lua_budget={} "
        "chunks={} error_seq={} script_error={}",
        id, frames_, paused_ ? "paused" : "running", params_.medium_live ? "live" : "fixed",
        rv_pcdev_hex(disc_id), code_hash, RV_MPPC_VER_MAJOR, RV_MPPC_VER_MINOR, script.reloadable ? 1 : 0,
        script.revision, script.hash, script.used, script.budget, script.slots, script.error_seq,
        rv_pcdev_hex(script.error)));
}

void rv_3dmppc::rv_pconsole::dev_reload(const rv_pcdevreq &req)
{
    // `entry` is a literal selector, not a name: version 1 replaces the entry
    // chunk and nothing else. A chunk the disc raised itself has a handle only
    // the disc knows, and inventing a lookup for it would be answering a
    // question nobody has asked yet.
    if (req.arg(0) != "entry") {
        dev_->reply(rv_pcdev_err(req.id, "unsupported_target", RV_ERR_INVAL, false,
            "this protocol version reloads the entry chunk only"));
        return;
    }

    rv_pccl_reload_report report;
    int64_t rc = 0;
    if (req.has_payload) {
        // Compiled under the entry's own asset name, because that name is what
        // lua puts in front of every error message the chunk produces.
        rc = cl_->script_reload_entry(req.payload.data(), static_cast<int64_t>(req.payload.size()),
            script_entry_.empty() ? "reload" : script_entry_.c_str(), report);
    } else {
        if (!params_.medium_live) {
            // Re-reading an archive entry would answer ok and change nothing:
            // the bytes in a zip cannot have moved. Refusing says so instead of
            // costing a frame to prove it.
            dev_->reply(rv_pcdev_err(req.id, "unsupported_medium", RV_ERR_INVAL, false,
                "the mounted medium cannot change; send the bytes, or boot an unpacked directory"));
            return;
        }
        rc = cl_->script_reload_entry_from_drive(report);
    }

    if (rc < 0) {
        dev_->reply(rv_pcdev_err(req.id, report.phase, rc, report.effects_possible, report.message));
        return;
    }

    rv_pccl_status script;
    cl_->script_status(script);
    dev_->reply(std::format("{} ok entry_revision={} entry_hash={:016x} lua_used={}", req.id,
        script.revision, script.hash, script.used));
}

void rv_3dmppc::rv_pconsole::dev_asset(const rv_pcdevreq &req)
{
    const std::string_view name = req.arg(0);
    if (name.empty()) {
        dev_->reply(rv_pcdev_err(req.id, "protocol", RV_ERR_INVAL, false,
            "asset needs the name of an entry on the mounted medium"));
        return;
    }
    if (!params_.medium_live) {
        // An archive entry cannot have changed, so there is nothing to refresh
        // and telling the game otherwise would have it re-upload the same bytes
        // and report success.
        dev_->reply(rv_pcdev_err(req.id, "unsupported_medium", RV_ERR_INVAL, false,
            "the mounted medium cannot change; boot an unpacked directory"));
        return;
    }

    // Resolved here so the answer can tell "there is no such entry" from "the
    // game refused it". The drive's name table is fixed at boot, which is also
    // why an ADDED asset needs a restart while a CHANGED one does not.
    const std::string key(name);
    if (cd_->asset_open(key.c_str()) < 0) {
        dev_->reply(rv_pcdev_err(req.id, "no_asset", RV_ERR_NOENT, false,
            "the mounted medium has no entry by that name"));
        return;
    }

    // From here the console's part is over: it has said which entry moved. What
    // that entry IS, where its bytes went, and whether anything still points at
    // them is knowledge that lives only in the game's own code.
    rv_pccl_reload_report report;
    const int64_t rc = cl_->script_asset_changed(key.c_str(), report);
    if (rc < 0) {
        dev_->reply(rv_pcdev_err(req.id, report.phase, rc, report.effects_possible, report.message));
        return;
    }
    dev_->reply(std::format("{} ok asset={}", req.id, rv_pcdev_hex(key)));
}

void rv_3dmppc::rv_pconsole::dev_get(const rv_pcdevreq &req)
{
    const std::string_view key = req.arg(0);
    if (key.empty()) {
        dev_->reply(rv_pcdev_err(req.id, "protocol", RV_ERR_INVAL, false, "get needs a key"));
        return;
    }

    rv_pccl_value value;
    const int64_t rc = cl_->state_get(std::string(key).c_str(), value);
    if (rc < 0) {
        dev_->reply(rv_pcdev_err(req.id, "no_machine", rc, false, "this disc declared no lua machine"));
        return;
    }

    switch (value.type) {
    case RV_CL_TYPE_BOOLEAN:
        dev_->reply(std::format("{} ok found=1 type=boolean value={}", req.id, value.boolean ? 1 : 0));
        return;
    case RV_CL_TYPE_NUMBER:
        dev_->reply(std::format("{} ok found=1 type=number value={}", req.id, value.number));
        return;
    case RV_CL_TYPE_STRING:
        // Hex, not text: a stored string may hold a NUL or bytes that are not
        // valid UTF-8, and the protocol promises to hand back what is there.
        dev_->reply(std::format("{} ok found=1 type=string value={}", req.id,
            rv_pcdev_hex(value.bytes)));
        return;
    case RV_CL_TYPE_TABLE:
        dev_->reply(std::format("{} ok found=1 type=table", req.id));
        return;
    case RV_CL_TYPE_FUNCTION:
        // Worth its own type name rather than "other": a function in state
        // keeps the old chunk's code alive and callable past a reload, which is
        // the one state-table mistake that looks like nothing at all.
        dev_->reply(std::format("{} ok found=1 type=function", req.id));
        return;
    case RV_CL_TYPE_OTHER:
        dev_->reply(std::format("{} ok found=1 type=other", req.id));
        return;
    default:
        // A lua table stores no nil, so "no such key" and "nil" are one fact.
        dev_->reply(std::format("{} ok found=0 type=nil", req.id));
        return;
    }
}

// --- C contract (pdk/rv_pdko.h) ----------------------------------------------
// The facade through which a disc sees the machine. An rv_pdko* handle is the
// address of an rv_pconsole: there is one console in the process, and it is the
// only implementation of the facade.
//
// Each of these returns the address of a controller field cast to the contract's
// opaque type. The reverse cast lives in rv_pcXX.cpp next to the controller
// itself, so both ends of every pair are visible in their own file.

extern "C" rv_ca *rv_pdko_ca(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->ca();
}
extern "C" rv_cd *rv_pdko_cd(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->cd();
}
extern "C" rv_cio *rv_pdko_cio(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->cio();
}
extern "C" rv_cl *rv_pdko_cl(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->cl();
}
extern "C" rv_cm *rv_pdko_cm(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->cm();
}
extern "C" rv_cv *rv_pdko_cv(rv_pdko *o)
{
    return reinterpret_cast<rv_3dmppc::rv_pconsole *>(o)->cv();
}
