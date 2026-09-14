#include "rv_pconsole/cio/rv_pccio_std.hpp"

#include <algorithm>

#include "pdk/cio/rv_cio.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/platform/rv_pcplatform.hpp"

namespace rv_3dmppc
{

rv_pcbudget_cost rv_pccio_std::evaluate(const rv_pdklib::rv_manifest_budget &budget)
{
    rv_pcbudget_cost cost;
    int64_t total = 0;
    if (rv_pcbudget_mul(cost, "budget.pccio.iport_count", budget.pccio.iport_count,
            static_cast<int64_t>(sizeof(rv_pccio_std_port)), total) ||
        rv_pcbudget_add(cost, "budget.pccio.iport_count", total)) {
        return cost;
    }
    return cost;
}

int64_t rv_pccio_std::iport_count()
{
    return conf_.iport_count;
}

// Release ports of departed pads, then give each arrived pad the first empty
// port. A pad that arrives while every port is full stays ignored until it
// reconnects; a reconnect is a new arrival and takes the first empty port.
void rv_pccio_std::reconcile()
{
    if (gamepads_.generation() == seen_generation_) {
        return;
    }

    const std::vector<uint32_t> &connected = gamepads_.connected();

    for (auto &port : ports_) {
        if (port.pad == 0) {
            continue;
        }
        bool still_connected = false;
        for (uint32_t id : connected) {
            if (id == port.pad) {
                still_connected = true;
                break;
            }
        }
        if (!still_connected) {
            RV_LOG_INFO("pccio", "port {} emptied", static_cast<int64_t>(&port - &ports_[0]));
            port.pad = 0;
        }
    }

    for (uint32_t id : connected) {
        if (std::find(seen_pads_.begin(), seen_pads_.end(), id) != seen_pads_.end()) {
            continue;
        }

        bool placed = false;
        for (auto &port : ports_) {
            if (port.pad == 0) {
                port.pad = id;
                RV_LOG_INFO("pccio", "gamepad {} adopted into port {}", id,
                    static_cast<int64_t>(&port - &ports_[0]));
                placed = true;
                break;
            }
        }
        if (!placed) {
            RV_LOG_WARN("pccio", "every port slot is occupied, gamepad {} ignored", id);
        }
    }

    seen_pads_ = connected;
    seen_generation_ = gamepads_.generation();
}

// The keyboard is only ever overlaid onto port 0. Logs a change of state
// (present <-> absent) whenever it flips, so a run's log shows exactly when a
// window (and so a keyboard) came or went.
uint64_t rv_pccio_std::keyboard_abilities()
{
    const uint64_t abilities = window_.keyboard_abilities();
    const int present = (abilities != 0) ? 1 : 0;
    if (present != keyboard_seen_) {
        if (present) {
            RV_LOG_INFO("pccio", "keyboard overlay on port 0: on");
        } else {
            RV_LOG_INFO("pccio", "keyboard overlay on port 0: off (no window)");
        }
        keyboard_seen_ = present;
    }
    return abilities;
}

// Static capability mask. An empty or out-of-range slot reads as 0 — the
// contract's "the query methods report data, not status": there is no error
// channel here to report a bad index through, and a disc that probes port 7 on
// a two-port machine legally gets "this port can do nothing".
uint64_t rv_pccio_std::iport_abilities(int64_t port)
{
    if (!port_in_range(port)) {
        return 0;
    }
    reconcile();

    const rv_pccio_std_port &slot = ports_[static_cast<size_t>(port)];
    uint64_t abilities = (slot.pad != 0) ? gamepads_.abilities(slot.pad) : 0;
    if (port == 0) {
        abilities |= keyboard_abilities();
    }
    return abilities;
}

// Relative motion since the previous call; the platform window owns the
// accumulator and clears it here (rv_cio::imouse is a CONSUMING read).
rv_imouse rv_pccio_std::imouse()
{
    return window_.consume_mouse();
}

// Instantaneous snapshot. Note the contract gives a LEVEL, never an edge: a disc
// that wants "just pressed" diffs successive snapshots itself.
rv_istate rv_pccio_std::iport_state(int64_t port)
{
    if (!port_in_range(port)) {
        return rv_istate{};
    }
    reconcile();

    const rv_pccio_std_port &slot = ports_[static_cast<size_t>(port)];
    rv_istate state = (slot.pad != 0) ? gamepads_.state(slot.pad) : rv_istate{};

    if (port == 0 && keyboard_abilities() != 0) {
        const rv_istate kb = window_.keyboard_state();
        state.buttons |= kb.buttons;
        if (kb.left_stick.x != 0.0F || kb.left_stick.y != 0.0F) {
            state.left_stick = kb.left_stick;
        }
        if (kb.right_stick.x != 0.0F || kb.right_stick.y != 0.0F) {
            state.right_stick = kb.right_stick;
        }
    }

    return state;
}

// PATTERN: tagged-union dispatch. rv_oheffect::type is a TAG (exactly one kind
// per call), not a combinable mask, so this is a switch that selects both the
// active `data` member and the gamepads call — never a loop over set bits.
//
// This is the only method in rv_cio with an error channel, so it is also the
// only place a bad port index is a failure rather than a zero read.
int64_t rv_pccio_std::ohaptic(int64_t port, rv_oheffect effect)
{
    if (!port_in_range(port)) {
        RV_LOG_WARN("pccio", "ohaptic on out-of-range port {} (count {})", port, conf_.iport_count);
        return RV_ERR_INVAL;
    }

    reconcile();

    const rv_pccio_std_port &slot = ports_[static_cast<size_t>(port)];
    if (slot.pad == 0) {
        return RV_ERR_INVAL;
    }

    switch (effect.type) {
    case RV_HAPTIC_EFFECT_BASIC_RUMBLE:
        // Body actuators: constant strength per side, held for a duration.
        // gamepads_.rumble reports RV_ERR_INVAL for an unknown id, which is
        // exactly the code the contract asks for.
        return gamepads_.rumble(slot.pad, effect.data.rumble.strength_left,
            effect.data.rumble.strength_right, effect.data.rumble.duration_ms);

    case RV_HAPTIC_EFFECT_TRIGGER_RUMBLE:
        // Same payload, different actuators (the trigger motors of a
        // DualSense / Steam Deck style pad).
        return gamepads_.rumble_triggers(slot.pad, effect.data.rumble.strength_left,
            effect.data.rumble.strength_right,
            effect.data.rumble.duration_ms);

    case RV_HAPTIC_EFFECT_LEFT_RIGHT_PULSE:
        // DEFERRED. The payload is a TIMED pulse train (on_time_us /
        // off_time_us / repeat_count), i.e. a small effect that has to be
        // stepped across frames — the platform currently offers only fire-
        // and-forget rumble with a duration, and there is no effect scheduler
        // to hang the train on. Rejecting is honest; faking it with one long
        // buzz would lie about what the machine did.
        return RV_ERR_INVAL;

    case RV_HAPTIC_EFFECT_WAVEFORM:
        // DEFERRED in the contract itself: rv_ohaptic.hpp defines no payload
        // for this tag yet, so there is nothing to forward.
        return RV_ERR_INVAL;

    default:
        RV_LOG_WARN("pccio", "ohaptic with unknown effect tag {} on port {}", effect.type,
            port);
        return RV_ERR_INVAL;
    }
}

} // namespace rv_3dmppc
