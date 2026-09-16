#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pdk/de/rv_de.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cio/rv_pccio.hpp"
#include "rv_pconsole/cl/rv_pccl.hpp"
#include "rv_pconsole/cm/rv_pccm.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/platform/rv_pcdevchan.hpp"
#include "rv_pconsole/platform/rv_pcplatform.hpp"
#include "rv_pconsole/rv_pcloader.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

// Composition root. This is the single place where the concrete
// machine is assembled - the platform it is served by, the six controllers,
// and the geometry they were built from. Nothing below constructs a
// subsystem: a controller receives what it needs and never reaches sideways
// for it. There is no host: the console talks only to rv_pcplatform.
// There is nothing left to inherit: rv_pdko is an opaque C type now, and "this
// console IS the facade" is expressed not by a base class but by the free
// rv_pdko_* functions at the end of rv_pconsole.cpp casting the handle to
// exactly this class.
class rv_pconsole
{
private:
    rv_pconsole_params params_;

    // BORROWED. Boot creates the platform after the budget check - nothing in
    // the budget depends on it - and it outlives this console. cio_ borrows
    // its window and gamepads in turn, which only works because the caller
    // is guaranteed to outlive this console.
    rv_pcplatform &platform_;

    // DECLARATION ORDER MATTERS, exactly as it does for platform_ above. Fields
    // are destroyed in the REVERSE order of their declaration, and cl_ is
    // deliberately LAST - not alphabetical - in this row: a Lua finaliser can
    // call back into rv_cv_*/rv_ca_* through FFI while the machine shuts
    // down, so lua_close() must run BEFORE any controller it might reach is
    // gone. Alphabetical placement would put cl_ ahead of cm_ and cv_ in
    // destruction order, which is a use-after-free.
    std::unique_ptr<rv_pcca> ca_;
    std::unique_ptr<rv_pccd> cd_;
    std::unique_ptr<rv_pccio> cio_;
    std::unique_ptr<rv_pccm> cm_;
    std::unique_ptr<rv_pccv> cv_;
    std::unique_ptr<rv_pccl> cl_;

    // BORROWED, never owned. The loader reads the manifest BEFORE this console
    // exists - the numbers it finds are what this console is built from - so it
    // cannot live inside the thing it configures. main() owns it and must let it
    // die FIRST: its teardown runs disc_shutdown(), a hook allowed to touch every
    // controller above. Null when the built-in disc is running.
    rv_pcloader *loader_ = nullptr;

    // The per-frame PCM scratch buffer, sized once at construction to the
    // largest a single frame can ever need: (RV_PCCA_PCM_RATE /
    // target_fps + 1) stereo frames, so a fractional-remainder frame from the
    // Bresenham accumulator in disc_run never overruns it.
    // Host scratch of the console, sized by target_fps; outside every module budget.
    std::vector<int16_t> pcm_;

    // --- the development runtime ------------------------------------------
    //
    // Constructed only when the run asked for it. Everything below is inert
    // without it: one `if` per frame that is not taken, and no second frame
    // loop - a dev path that diverged from the ordinary one would drift, and
    // then the thing the developer tested would not be the thing that ships.
    std::optional<rv_pcdevchan> dev_;

    // The entry chunk's asset name, kept so a candidate that arrived over the
    // channel can be compiled under the name the developer recognises: it is
    // what lua puts in front of every error message from that chunk.
    std::string script_entry_;

    // Stopped, whoever asked. Two inputs reach this one flag: the `pause`
    // request on the development channel, and the physical Pause key, which is
    // an operator's act on the machine (like closing the window) rather than
    // game input and therefore needs no --dev. One flag and not two, so there
    // is one answer to "is this machine running" no matter who stopped it.
    bool paused_ = false;

    // A step is armed by ONE request and answered after ITS frame, so three
    // step requests are three frames and three answers. A counter would let
    // them collapse into one frame; the queue is suspended at a step instead,
    // which is also why the reply id has to be remembered rather than answered
    // on the spot.
    int64_t step_reply_id_ = -1;

    // The picture a stopped console presents: the last frame dimmed, with
    // CONSOLE PAUSED across it. A copy, so the disc's own last frame - and
    // therefore --dump-frame - stays exactly what the disc drew. Allocated the
    // first time the machine is actually stopped, and never in a headless run.
    std::vector<uint32_t> pause_overlay_;
    bool pause_overlay_valid_ = false;

    uint64_t frames_ = 0;
    bool quit_by_command_ = false;
    bool dev_close_logged_ = false;

    // The last script-error sequence number this console has already reacted
    // to. rv_pccl counts every failed hook call; comparing against that count
    // is how the console learns a game hook broke, without the disc having to
    // tell it and without a contract change.
    int64_t dev_error_seq_ = 0;

    // Move the channel along and execute whatever arrived, on the frame
    // boundary and nowhere else: at that point no script call is in flight and
    // the lua stack is at its base, which is what makes a code swap safe. Pause
    // is a convenience for the developer, never a precondition.
    // --- the frame loop, in named steps -----------------------------------
    //
    // How the next frame's START TIME is decided. The frame's DURATION is
    // always 1/target_fps and never varies with the wall clock or the audio
    // device; pacing only decides when that frame runs.
    enum class pacing { none, audio, clock };

    // One run's bookkeeping. A struct handed between the steps below rather
    // than a row of members: none of it outlives disc_run, and as members a
    // second run would inherit the first one's audio counters and its stale
    // deadline.
    struct run_state {
        uint64_t target_fps = 60;
        float dt = 0.0f;
        std::chrono::duration<double> frame_budget{};
        pacing mode = pacing::none;
        int64_t queue_target = 0;
        std::chrono::steady_clock::time_point deadline{};

        // The previous iteration created no frame, so the pacing deadline is
        // stale and has to be re-based before it is believed again.
        bool left_pause = false;

        int64_t audio_phase = 0;
        int64_t audio_written = 0;
        int64_t audio_underruns = 0;
        int64_t audio_peak_queued = 0;
        bool audio_paced_ever = false;
    };

    static const char *pacing_name(pacing mode);

    // Open the window, start the disc, settle the timeline and arm the
    // development runtime. A negative return means the disc refused to start
    // and there is no loop to enter.
    int64_t run_start(rv_de *disc, run_state &run);

    // The operator's stop switch, read straight off the platform.
    void run_pause_key();

    // Serve whatever the development channel has to say on this boundary.
    // True means it asked the console to stop. A step of its own rather than
    // four lines inside the loop: the loop body should read as a flat list of
    // what happens per frame, and every `if` nested in it is one more thing a
    // reader has to hold while looking for the timing rule.
    bool run_dev_commands();

    // True when this iteration creates NO frame. Also owns what a pause does
    // to the clock, because the two are the same fact seen twice.
    bool run_hold_paused(run_state &run);

    // One frame of the machine: update, render, present, and the audio of
    // exactly that step.
    void run_frame(rv_de *disc, run_state &run);

    // What the console owes the channel once the frame is over.
    void run_after_frame();

    // Wait, however this run decides to wait.
    void run_pace(run_state &run);

    // The last hook, the frame dump, the audio summary and the last answer.
    void run_finish(rv_de *disc, const run_state &run);

    void dev_service();
    void dev_dispatch(const rv_pcdevreq &req);
    void dev_status(int64_t id);
    void dev_reload(const rv_pcdevreq &req);
    void dev_get(const rv_pcdevreq &req);
    void dev_asset(const rv_pcdevreq &req);

public:
    rv_pconsole(const rv_pconsole_conf &conf, rv_pcplatform &platform, rv_pcloader *loader);

    ~rv_pconsole() = default;

    rv_ca *ca();
    rv_cd *cd();
    rv_cio *cio();
    rv_cm *cm();
    rv_cv *cv();
    rv_cl *cl();

    // The drive itself, console-side. rv_pdko::cd() hands a disc the CONTRACT's
    // view (rv_cd, which cannot load a medium); putting a medium IN the drive is
    // the machine operator's act, not the disc's, so it goes through here.
    rv_pccd &drive()
    {
        return *cd_;
    }

    // Inversion of control. The frame loop belongs to the console; the
    // disc lives inside the rv_de hooks and never owns a loop of its own.
    //
    // Returns RV_OK when the run ends normally (the disc asked to stop, the
    // frame budget ran out, or the user powered the machine off), or a negative
    // rv_err if the disc refused to initialize. A display that would not come
    // up is a warning, not a stop.
    int64_t disc_run(rv_de *disc);

    // Did every resource this console was built from actually come into
    // existence? Covers every slot: ca_, cd_, cio_, cm_, cv_ and
    // cl_. A budget the boot budget check accepted can still fail to
    // materialise in the constructor - an address-space reservation is allowed to
    // refuse, and so is the card's backing image. cl_ needs no
    // special-casing: rv_pccl::valid() already treats "scripting was never
    // asked for" as true, so this stays a plain conjunction. False means the
    // machine did not provide what the disc declared, and the run must not
    // start.
    bool ready() const;
};

} // namespace rv_3dmppc
