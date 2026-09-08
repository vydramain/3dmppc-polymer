#include "rv_pconsole.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cd/rv_pczipmedium.hpp"

namespace rv_3dmppc
{
namespace
{

// THEOREM: dt clamping. A frame that stalls (window drag, a breakpoint, the
// machine going to sleep) hands the disc an enormous dt, and every integration
// the disc does — movement, timers, physics — teleports. Saturating dt turns a
// stall into slow motion, which is recoverable, instead of a jump, which is not.
// The cap is deliberately generous: it must never fire during normal play.
constexpr float RV_PCONSOLE_DT_CEILING = 0.25f;

using rv_pcclock = std::chrono::steady_clock;

} // namespace
} // namespace rv_3dmppc

rv_3dmppc::rv_pconsole::rv_pconsole(const rv_3dmppc::rv_pconsole_conf &conf,
    rv_3dmppc::rv_pchost &host, rv_3dmppc::rv_pcloader *loader)
    : params_(conf.params)
    , host_(host)
    , ca_(conf.ca, host_)
    , cd_(conf.cd)
    , cio_(conf.cio, host_)
    , cm_(conf.cm)
    , cv_(conf.cv, host_)
    , cl_(conf.cl, cd_)
    , loader_(loader)
{
}

// This is where the contract meets the machine. Every line casts the address of
// a concrete controller to the contract's opaque type; that is legal for exactly
// one reason — a single implementation of each controller lives in the process,
// and the reverse cast in rv_pcXX.cpp hands back that very same address.
rv_ca *rv_3dmppc::rv_pconsole::ca()
{
    return reinterpret_cast<rv_ca *>(&ca_);
}
rv_cd *rv_3dmppc::rv_pconsole::cd()
{
    return reinterpret_cast<rv_cd *>(&cd_);
}
rv_cm *rv_3dmppc::rv_pconsole::cm()
{
    return reinterpret_cast<rv_cm *>(&cm_);
}
rv_cio *rv_3dmppc::rv_pconsole::cio()
{
    return reinterpret_cast<rv_cio *>(&cio_);
}
rv_cv *rv_3dmppc::rv_pconsole::cv()
{
    return reinterpret_cast<rv_cv *>(&cv_);
}
rv_cl *rv_3dmppc::rv_pconsole::cl()
{
    // Absent, not present-and-reporting-absent: a disc that never declared
    // [budget.pccl] gets no handle at all, the same way its own manifest
    // omits that section rather than writing it as zeros.
    return cl_.scripting() ? reinterpret_cast<rv_cl *>(&cl_) : nullptr;
}

bool rv_3dmppc::rv_pconsole::ready() const
{
    return ca_.valid() && cv_.valid() && cm_.valid() && cl_.valid();
}

int64_t rv_3dmppc::rv_pconsole::disc_run(rv_de *disc)
{
    // Everything a disc needs must be ready before its code runs, so the
    // window is opened HERE, before disc_initialize() below — never after.
    // disc_title() is a plain accessor (pdk/de/rv_de.h) with no dependency
    // on disc_initialize() having run, so it is safe to call this early.
    //
    // Without --headless, a failure to bring video up is a WARNING, not a
    // stop: host_.presenting() stays false and host_.present() is a no-op,
    // so the disc runs unpresented instead of not running at all.
    if (!params_.headless) {
        const int64_t opened =
            host_.open(disc->disc_title(disc->self), cv_.screen_width(), cv_.screen_height(), params_.scale);
        if (0 > opened) {
            RV_LOG_WARN("pconsole",
                "display did not come up for '{}', continuing without presentation",
                disc->disc_title(disc->self));
        }
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
    // "loaded". A disc this loader did not produce is ignored — see below.
    if (loader_ != nullptr) {
        loader_->notify_initialized(disc);
    }

    const uint64_t target_fps = params_.target_fps ? params_.target_fps : 60;
    const std::chrono::duration<double> frame_budget{ 1.0 / static_cast<double>(target_fps) };
    const float fixed_dt = 1.0f / static_cast<float>(target_fps);

    RV_LOG_INFO("pconsole", "running mppcdisc '{}' ({}, {} fps target)", disc->disc_title(disc->self),
        params_.headless ? "headless" : "presented", target_fps);

    uint64_t frames = 0;
    rv_pcclock::time_point t_prev = rv_pcclock::now();
    rv_pcclock::time_point t_deadline =
        t_prev + std::chrono::duration_cast<rv_pcclock::duration>(frame_budget);

    for (;;) {
        // One pump per frame turns SDL's event stream into the instantaneous
        // port snapshots rv_cio hands the disc. It must happen before
        // frame_update, or the disc reads input that is one frame stale.
        host_.pump();

        // The power switch. Closing the window is the console's own shutdown
        // path — rv_de::disc_release() is the disc ASKING to stop, and pulling
        // the plug was never the disc's decision.
        if (host_.power_off()) {
            RV_LOG_INFO("pconsole", "powered off by the user after {} frame(s)", frames);
            break;
        }

        const rv_pcclock::time_point t_now = rv_pcclock::now();
        const float measured = std::chrono::duration<float>(t_now - t_prev).count();
        t_prev = t_now;

        // fixed_step feeds the disc exactly one tick regardless of wall clock,
        // which is what makes a headless run reproducible frame for frame.
        const float dt =
            params_.fixed_step ? fixed_dt : std::clamp(measured, 0.0f, RV_PCONSOLE_DT_CEILING);

        disc->frame_update(disc->self, dt);
        // --headless means no window AND no rasterization: frame_render() is
        // simply not called, so a headless run never touches the software
        // rasterizer, the framebuffer or the virtual VRAM (and --dump-frame
        // is refused together with --headless at argument-parsing time,
        // because there would be nothing rendered to dump). A run WITHOUT
        // --headless still renders every frame even when video merely failed
        // to come up — that run wanted a picture, it just has no screen to
        // put it on.
        if (!params_.headless) {
            disc->frame_render(disc->self);
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

        // Pacing exists to stop a presented console from burning a core to draw
        // frames nobody sees. A headless run is a smoke test — it goes flat out.
        if (host_.presenting()) {
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
    host_.dump_last_frame();

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
