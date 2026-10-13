// One iteration of the frame loop. The order of its steps is the timing contract;
// the pump, quit, console_must_stop_ and disc_release steps carry comments here, the
// run_* steps and cmd_after_frame are described at their declarations in rv_pconsole.hpp.
// The caller owns the loop and the pacing, this file owns neither.
#include "rv_pconsole/rv_pconsole.hpp"

#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

rv_pconsole::step_result rv_pconsole::frame_step(rv_de *disc, run_state &run)
{
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
        RV_LOG_INFO("pconsole", "shutdown requested (window closed or SIGINT/SIGTERM) after {} frame(s)", frames_);
        return step_result::stop;
    }

    run_pause_key();

    if (run_cmd_channel()) {
        return step_result::stop;
    }

    // Checked here, before run_frame: a frame on a state the console can no longer vouch for is not run.
    if (console_must_stop_) {
        RV_LOG_ERR("pconsole", "stopping after {} frame(s): the script state was not restored after a refused reload", frames_);
        return step_result::stop;
    }

    if (run_hold_paused(run)) {
        return step_result::idle;
    }

    // Only a pass that runs a frame pulls sound: a stopped machine must not advance pcca through the device.
    platform_.audio().service();

    run_frame(disc, run);
    cmd_after_frame();

    // Polled every frame, per the contract. Checked after the frame so the
    // disc gets to draw the frame on which it decided to quit.
    if (disc->disc_release(disc->self)) {
        RV_LOG_INFO("pconsole", "mppcdisc released after {} frame(s)", frames_ + 1);
        return step_result::stop;
    }

    ++frames_;
    if (params_.max_frames && frames_ >= params_.max_frames) {
        RV_LOG_INFO("pconsole", "frame budget of {} reached", params_.max_frames);
        return step_result::stop;
    }

    return step_result::frame;
}

} // namespace rv_3dmppc
