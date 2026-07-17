#pragma once

#include <cstdint>

#include "rv_imouse.hpp"
#include "rv_isource.hpp"
#include "rv_ohaptic.hpp"

namespace rv_3dmppc {

// Controller Input/Output: the console's controller ports (gamepad input +
// haptic output) plus the mouse. State is read as an instantaneous snapshot,
// hardware-style: the contract reports the CURRENT level of every source and
// never edges (`pressed`/`released`). A game derives edges itself by diffing
// successive snapshots — the console owns no notion of a frame here.
//
// A port is a STABLE slot: index N always denotes the same player. An empty
// slot reports zero abilities and a zeroed state; it is never an error and
// never renumbers the other slots.
//
// DEFERRED: the memory card (persistent save) is part of controller I/O on real
// hardware but is not surfaced here yet — see README "Status".
class rv_cio {
   public:
    virtual ~rv_cio() = default;

    // Bring the I/O subsystem up. Returns RV_OK, or a negative rv_err.
    // OPEN: whether a disc calls this, or the console has already done it before
    // boot, depends on who owns the frame loop — see README "Open decisions".
    virtual int init() = 0;

    // Count of controller port slots the console exposes. Slots are fixed and
    // stable; probe iport_abilities()/iport_state() to see which are populated.
    virtual int iports_size() = 0;

    // Bitmask of rv_isource values describing what `port` is CAPABLE of
    // reporting (static capability, e.g. "this controller has a gyro"). Returns
    // 0 for an empty or out-of-range slot. This is distinct from the live bits
    // in rv_istate::buttons, which report what a source is doing right now.
    virtual uint64_t iport_abilities(int port) = 0;

    // Mouse look channel: motion RELATIVE to the previous poll (variant B), not
    // an absolute cursor position. Suited to camera look; a UI cursor would need
    // a separate absolute-position query. The mouse is singular — no port index.
    virtual rv_imouse imouse() = 0;

    // Instantaneous state (buttons, sticks, triggers, motion) of `port`. Returns
    // a zeroed state for an empty or out-of-range slot.
    virtual rv_istate iport_state(int port) = 0;

    // Play a haptic effect on `port`'s controller — the output ("O") half of
    // controller I/O. Returns RV_OK, or a negative rv_err.
    virtual int ohaptic(int port, rv_oheffect effect) = 0;
};

}  // namespace rv_3dmppc
