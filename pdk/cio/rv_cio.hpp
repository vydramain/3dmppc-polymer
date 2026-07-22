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
// The query methods report data, not status: an empty or out-of-range port reads
// as zero rather than as an error. Only ohaptic() can fail.
//
// The memory card is NOT here: persistent save is its own controller (rv_cm) —
// storage semantics (rare calls, real errors, durable state) are the opposite
// of this snapshot-style contract.
class rv_cio {
   public:
    virtual ~rv_cio() = default;

    // Count of controller port slots the console exposes. Slots are fixed and
    // stable; probe iport_abilities()/iport_state() to see which are populated.
    virtual int64_t iport_count() = 0;

    // Bitmask of rv_isource values describing what `port` is CAPABLE of
    // reporting (static capability, e.g. "this controller has a gyro"). Returns
    // 0 for an empty or out-of-range slot. This is distinct from the live bits
    // in rv_istate::buttons, which report what a source is doing right now.
    virtual uint64_t iport_abilities(int64_t port) = 0;

    // Mouse look channel: motion RELATIVE to the previous poll (variant B), not
    // an absolute cursor position. Suited to camera look; a UI cursor would need
    // a separate absolute-position query. The mouse is singular — no port index.
    //
    // Each call reports the motion accumulated since the previous call. The
    // first call returns {0, 0}.
    virtual rv_imouse imouse() = 0;

    // Instantaneous state (buttons, sticks, triggers, motion) of `port`. Returns
    // a zeroed state for an empty or out-of-range slot.
    virtual rv_istate iport_state(int64_t port) = 0;

    // Play a haptic effect on `port`'s controller — the output ("O") half of
    // controller I/O. `effect` is taken by value. Returns RV_OK, or a negative
    // rv_err (RV_ERR_INVAL for an empty port or an unknown effect type).
    virtual int64_t ohaptic(int64_t port, rv_oheffect effect) = 0;
};

}  // namespace rv_3dmppc
