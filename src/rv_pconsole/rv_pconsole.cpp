#include "rv_pconsole.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#include "pdk/rv_err.hpp"
#include "rv_infra/rv_log.hpp"

namespace rv_3dmppc {
namespace {

// NEUROSLOP-BEGIN (claude-opus-5)
// THEOREM: dt clamping. A frame that stalls (window drag, a breakpoint, the
// machine going to sleep) hands the disc an enormous dt, and every integration
// the disc does — movement, timers, physics — teleports. Saturating dt turns a
// stall into slow motion, which is recoverable, instead of a jump, which is not.
// The cap is deliberately generous: it must never fire during normal play.
constexpr float RV_PCONSOLE_DT_CEILING = 0.25f;

using rv_pcclock = std::chrono::steady_clock;
// NEUROSLOP-END

}  // namespace
}  // namespace rv_3dmppc

// NEUROSLOP-BEGIN (claude-opus-5)
rv_3dmppc::rv_pconsole::rv_pconsole(const rv_3dmppc::rv_pconsole_conf& conf)
    : params_(conf.params),
      host_(conf),
      ca_(conf.ca, host_),
      cd_(conf.cd),
      cio_(conf.cio, host_),
      cm_(conf.cm),
      cv_(conf.cv, host_) {}
// NEUROSLOP-END

rv_3dmppc::rv_ca* rv_3dmppc::rv_pconsole::ca() { return &ca_; }
rv_3dmppc::rv_cd* rv_3dmppc::rv_pconsole::cd() { return &cd_; }
rv_3dmppc::rv_cm* rv_3dmppc::rv_pconsole::cm() { return &cm_; }
rv_3dmppc::rv_cio* rv_3dmppc::rv_pconsole::cio() { return &cio_; }
rv_3dmppc::rv_cv* rv_3dmppc::rv_pconsole::cv() { return &cv_; }

// rv_3dmppc::rv_de& rv_3dmppc::rv_pconsole::disc_load(const char* /*path*/) {};

int64_t rv_3dmppc::rv_pconsole::disc_run(rv_3dmppc::rv_de& disc) {
    int64_t dir = disc.disc_initialize(*this);
    if (0 > dir) {
        RV_LOG_ERR("pconsole",
                   "Can't initialize mppcdisc in console. Please check mppcdisc consistency: {}",
                   dir);
        return rv_3dmppc::RV_ERR_INVAL;
    }

    // NEUROSLOP-BEGIN (claude-opus-5)
    // A headless run never opens a display: the host stays a null object, so
    // frame_flush() presents to nothing and the loop below needs no branch of
    // its own beyond skipping frame_render() (which the rv_de contract already
    // declares is skipped when there is no output).
    if (!params_.headless) {
        const int64_t opened = host_.open(disc.disc_title(), params_.scale);
        if (0 > opened) {
            RV_LOG_ERR("pconsole", "display did not come up, refusing to run '{}'",
                       disc.disc_title());
            return opened;
        }
    }

    const uint64_t target_fps = params_.target_fps ? params_.target_fps : 60;
    const std::chrono::duration<double> frame_budget{1.0 / static_cast<double>(target_fps)};
    const float fixed_dt = 1.0f / static_cast<float>(target_fps);

    RV_LOG_INFO("pconsole", "running mppcdisc '{}' ({}, {} fps target)", disc.disc_title(),
                params_.headless ? "headless" : "presented", target_fps);

    uint64_t frames = 0;
    rv_pcclock::time_point t_prev = rv_pcclock::now();
    rv_pcclock::time_point t_deadline = t_prev + std::chrono::duration_cast<
                                                    rv_pcclock::duration>(frame_budget);

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

        disc.frame_update(dt);
        if (!params_.headless) disc.frame_render();

        // Polled every frame, per the contract. Checked after the frame so the
        // disc gets to draw the frame on which it decided to quit.
        if (disc.disc_release()) {
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
                t_deadline = after + std::chrono::duration_cast<rv_pcclock::duration>(
                                         frame_budget);
            }
        }
    }

    // Devkit: hand the last frame the machine produced to disk, if asked. After
    // the loop rather than inside it, so a dump costs nothing per frame.
    host_.dump_last_frame();
    // NEUROSLOP-END

    return rv_3dmppc::RV_OK;
}
