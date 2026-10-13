#pragma once

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "pdk/de/rv_de.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_manifest/rv_manifest.hpp"
#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cg/rv_pccg.hpp"
#include "rv_pconsole/cio/rv_pccio.hpp"
#include "rv_pconsole/cl/rv_pccl.hpp"
#include "rv_pconsole/cm/rv_pccm.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/platform/rv_pccmdchan.hpp"
#include "rv_pconsole/platform/rv_pcplatform.hpp"
#include "rv_pconsole/rv_pcloader.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

// Frame rate when the disc does not specify target_fps; used by both
// rv_pconsole.cpp and rv_pconsole_run.cpp to compute the default timeline.
constexpr uint64_t RV_PCONSOLE_DEFAULT_TARGET_FPS = 60;

// Bounded, best-effort wait at shutdown for the dev command channel: the last answer
// reaches a client still listening, and a client that is gone cannot hold the shutdown open.
constexpr auto RV_PCONSOLE_SHUTDOWN_DRAIN_TIMEOUT = std::chrono::milliseconds(50);

// Composition root. This is the single place where the concrete
// machine is assembled - the platform it is served by, the controllers,
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
    //
    // cd_ is placed right before cl_ - after cv_, not alphabetically - for the
    // same reason: ~rv_pccd_fs() frees every texture it made resident through
    // the borrowed cv_ (rv_pccd.hpp), so cv_ must still be alive when cd_ is
    // destroyed. cd_ still outlives cl_'s destruction, same as cv_/ca_, in
    // case a Lua finaliser reaches the drive too.
    std::unique_ptr<rv_pcca> ca_;
    std::unique_ptr<rv_pccio> cio_;
    std::unique_ptr<rv_pccm> cm_;
    std::unique_ptr<rv_pccv> cv_;
    // After cv_ so it dies first: a geometry frame puts its primitives into cv_.
    std::unique_ptr<rv_pccg> cg_;
    std::unique_ptr<rv_pccd> cd_;
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

    // Stereo frames the device has pulled from audio_fill this run; logged at the end.
    int64_t audio_device_frames_ = 0;

    // Time of the device's last pull, or of the run's start, or of its last resume from pause.
    // The console is single-threaded, so the device's pulls reach it on this thread only.
    std::chrono::steady_clock::time_point audio_pull_{};

    // --dump-audio: raw S16LE stereo of every block the sound module produced, device or frames.
    // Open only during a run that asked for it; a write error closes it and ends the dump.
    std::FILE *audio_dump_ = nullptr;
    int64_t audio_dump_frames_ = 0;
    bool audio_dump_failed_ = false;

    // --- the development runtime ------------------------------------------
    //
    // Constructed only when the run asked for it. Everything below is inert
    // without it: one `if` per frame that is not taken, and no second frame
    // loop - a dev path that diverged from the ordinary one would drift, and
    // then the thing the developer tested would not be the thing that ships.
    std::unique_ptr<rv_pccmdchan> cmd_;

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
    // A reload was refused AND its put-back of the script state failed, so the
    // live state is not what the running code expects. Set by the reload command,
    // checked once per frame boundary; the run ends with RV_ERR_IO.
    bool console_must_stop_ = false;
    // Move the channel along and execute whatever arrived, on the frame
    // boundary and nowhere else: at that point no script call is in flight and
    // the lua stack is at its base, which is what makes a code swap safe. Pause
    // is a convenience for the developer, never a precondition.
    // --- the frame loop, in named steps -----------------------------------
    //
    // How the next frame's START TIME is decided. The frame's DURATION is
    // always 1/target_fps and never varies with the wall clock or the audio
    // device; pacing only decides when that frame runs.
    enum class pacing {
        none,
        clock
    };

    // Where a frame's sound goes, decided once per run. The device pulls its own
    // sound from audio_fill; without one the frame loop advances the machine itself.
    enum class sound_out {
        device,
        frames
    };

    // One run's bookkeeping. A struct handed between the steps below rather
    // than a row of members: none of it outlives disc_run, and as members a
    // second run would inherit the first one's stale deadline.
    struct run_state {
        uint64_t target_fps = 60;
        float dt = 0.0f;
        std::chrono::duration<double> frame_budget{};
        pacing mode = pacing::none;
        sound_out sound = sound_out::frames;
        std::chrono::steady_clock::time_point deadline{};

        // The previous iteration created no frame, so the pacing deadline is
        // stale and has to be re-based before it is believed again.
        bool left_pause = false;

        // Bresenham remainder for sound_out::frames: keeps the sample count exact.
        int64_t audio_phase = 0;

        // The device went silent and the frame loop took the sound; run_finish still reports the device.
        bool device_stalled = false;
    };

    static const char *pacing_name(pacing mode);
    static const char *sound_name(sound_out sound);

    // The device asks for the sound it is owed: fills exactly `frames` stereo frames.
    static void audio_fill(void *context, int16_t *interleaved, int64_t frames);

    // Open the --dump-audio file if one was asked for. A refusal names the path and the cause.
    int audio_dump_open();
    // Append one block of stereo frames; a write error logs its cause and ends the dump.
    void audio_dump_write(const int16_t *interleaved, int64_t frames);
    // Close the dump file; a failed close ends the dump with a logged cause.
    void audio_dump_close();

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
    bool run_cmd_channel();

    // True when this iteration creates NO frame. Also owns what a pause does
    // to the clock, because the two are the same fact seen twice.
    bool run_hold_paused(run_state &run);

    // A device that pulled nothing for RV_PCONSOLE_AUDIO_STALL hands the sound to the
    // frame loop for the rest of the run. Logs the switch; a device never gets it back.
    void run_audio_stall(run_state &run);

    // Show the pause overlay (build once, present every slice).
    void run_show_pause();

    // One frame of the machine: update, render, present, and the audio of
    // exactly that step.
    void run_frame(rv_de *disc, run_state &run);

    // Clock pacing: sleep to the next deadline. Unpaced runs never wait.
    void run_pace(run_state &run);

    // Detach the device source, log the audio summary and the last hook, after the last frame.
    void run_finish(rv_de *disc, const run_state &run);

    // Write the last frame the machine produced to the requested path. Called only
    // when a dump was asked for; a refusal is logged with its cause and returned.
    int run_dump_frame();

    // The last answer to a client that is still there. Bounded, best effort.
    void run_release_channel();

    // What one iteration of the loop produced: a frame, an idle iteration (paused), or the end of the run.
    enum class step_result {
        frame,
        idle,
        stop
    };

    // One iteration of the frame loop; the order of its steps is commented in rv_pconsole_step.cpp.
    // Holds no loop and no pacing: the caller decides when the next iteration runs and what waits between.
    step_result frame_step(rv_de *disc, run_state &run);

    // Every one of these is a SLOT: rv_pconsole_cmd_devtools.cpp in a development build,
    // rv_pconsole_cmd_standard.cpp in a player build, chosen in CMakeLists.txt.
    // The frame loop calls them unconditionally so there is one loop and not
    // two - what a developer tested is what ships - and a player binary
    // carries no line of what they say.
    void cmd_service();
    // What the console owes the channel once the frame is over: the answer to
    // a step, and a game hook that failed this frame.
    void cmd_after_frame();
    // Announce each scene the disc opened since the client last heard; a new scene generation starts the list over.
    void cmd_send_scenes();
    // The Pause KEY moved the machine. The client did not ask, so it hears
    // about it as an event.
    void cmd_note_pause();
    void cmd_dispatch(const rv_pccmdreq &req);
    // Answers every request still queued with a refusal once console_must_stop_ is set.
    void cmd_refuse_pending();
    void cmd_status(int64_t id);
    void cmd_reload(const rv_pccmdreq &req);
    void cmd_reload_module(const rv_pccmdreq &req);
    void cmd_get(const rv_pccmdreq &req);
    void cmd_keys(const rv_pccmdreq &req);
    void cmd_asset(const rv_pccmdreq &req);
    // An asset request that carries its bytes: refresh the resident texture from them, not from the medium.
    void cmd_asset_from_bytes(const rv_pccmdreq &req, const std::string &key);

public:
    rv_pconsole(const rv_pconsole_conf &conf, rv_pcplatform &platform, rv_pcloader *loader);

    ~rv_pconsole() = default;

    rv_ca *ca();
    rv_cd *cd();
    rv_cio *cio();
    rv_cm *cm();
    rv_cv *cv();
    rv_cg *cg();
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
    // frame budget ran out, or the user powered the machine off), a negative
    // rv_err if the disc refused to initialize, RV_ERR_IO if a refused reload left
    // the script state unrestored, or the rv_err of a requested frame dump that
    // failed, or RV_ERR_IO if a requested audio dump failed. A display that would not come up is a warning, not a stop.
    int64_t disc_run(rv_de *disc);

    // Did every resource this console was built from actually come into
    // existence? Covers every slot: ca_, cd_, cio_, cm_, cv_, cg_ and cl_. A budget the
    // boot budget check accepted can still fail to materialise in the constructor.
    // cl_ needs no special case: rv_pccl::valid() is true when scripting was never
    // asked for. RV_OK, or that controller's code after logging the first one that is
    // not usable: RV_ERR_NOMEM for memory it could not reserve, RV_ERR_IO for cm, whose
    // card image is a file that is unreadable or has refused geometry. The machine did
    // not provide what the disc declared and the run must not start.
    int check_ready() const;
};

} // namespace rv_3dmppc
