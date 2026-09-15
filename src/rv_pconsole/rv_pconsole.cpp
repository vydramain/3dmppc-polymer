#include "rv_pconsole.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "pdk/rv_err.h"
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

    uint64_t frames = 0;
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

    return RV_OK;
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
