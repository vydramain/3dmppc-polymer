#pragma once

#include "pdk/rv_pdko.hpp"

namespace rv_3dmppc {

// Disc Entry — the second direction of the PDK contract: the hooks a game
// implements and the console drives (rv_pdko is the opposite arrow — what the
// game calls). By convention every disc implements this interface in a class
// named rv_dmain.
//
// The console owns the frame loop; the game exists only inside these hooks — it
// has no main() and no loop of its own. Each hook must do one call's worth of
// work and RETURN: the console cannot preempt a hook that never comes back.
//
// Lifecycle, driven by the console (rv_pconsole::disc_run):
//   disc_initialize(pdko)                   — once, before the first frame
//   loop: frame_update(dt); frame_render()  — until disc_release() is true
//
// DEFERRED: a teardown hook for the dlclose era (under a name other than
// disc_release, which is taken by the power-off query).
class rv_de {
   public:
    virtual ~rv_de() = default;

    // Called once, before the first frame. The disc stashes the facade
    // (rv_pdko* — borrowed, valid until the console tears the disc down),
    // queries the hardware geometry (screen_width(), card_slot_size(),
    // voice_count(), ...) and VALIDATES its own baked assumptions against the
    // answers, then uploads its assets and reads its save.
    //
    // Returns >= 0 to start, or a negative rv_err — the console then refuses to
    // run the disc and reports to the user. rv_err starts here: earlier
    // failures (mounting, ABI) belong to the console and never reach the game.
    virtual int64_t disc_initialize(rv_pdko& pdk) = 0;

    // Advance the simulation by `dt` seconds — ONE frame's worth of work, then
    // return. Input is pulled from the facade (io()->iport_state(...)); there
    // is no input argument.
    virtual void frame_update(float dt) = 0;

    // Build the frame: frame_configure(), frame_put() the primitives, and END
    // WITH frame_flush() — the console does not flush for the disc. Skipped
    // entirely in headless runs, so no simulation state may live here.
    virtual void frame_render() = 0;

    // Power-off QUERY, polled by the console every frame: true = the disc asks
    // the console to shut down. It releases nothing itself.
    virtual bool disc_release() const { return false; }

    // Name for the window title / logs. The pointer must stay valid for the
    // disc's whole lifetime (a string literal).
    virtual const char* disc_title() const = 0;
};

}  // namespace rv_3dmppc
