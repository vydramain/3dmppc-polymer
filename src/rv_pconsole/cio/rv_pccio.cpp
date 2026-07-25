// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 3 — реализация rv_cio: диапазоны портов и диспетчер гаптики.
// ──────────────────────────────────────────────────────────────────────────────
#include "rv_pconsole/cio/rv_pccio.hpp"

#include "pdk/rv_err.hpp"
#include "rv_infra/rv_log.hpp"

namespace rv_3dmppc {

int64_t rv_pccio::iport_count() { return conf_.iport_count; }

// Static capability mask. An empty or out-of-range slot reads as 0 — the
// contract's "the query methods report data, not status": there is no error
// channel here to report a bad index through, and a disc that probes port 7 on
// a two-port machine legally gets "this port can do nothing".
uint64_t rv_pccio::iport_abilities(int64_t port) {
    if (!port_in_range(port)) return 0;
    return host_.port_abilities(port);
}

// Relative motion since the previous call; the host owns the accumulator and
// clears it here (rv_cio::imouse is a CONSUMING read).
rv_imouse rv_pccio::imouse() { return host_.consume_mouse(); }

// Instantaneous snapshot. Note the contract gives a LEVEL, never an edge: a disc
// that wants "just pressed" diffs successive snapshots itself.
rv_istate rv_pccio::iport_state(int64_t port) {
    if (!port_in_range(port)) return rv_istate{};
    return host_.port_state(port);
}

// PATTERN: tagged-union dispatch. rv_oheffect::type is a TAG (exactly one kind
// per call), not a combinable mask, so this is a switch that selects both the
// active `data` member and the host call — never a loop over set bits.
//
// This is the only method in rv_cio with an error channel, so it is also the
// only place a bad port index is a failure rather than a zero read.
int64_t rv_pccio::ohaptic(int64_t port, rv_oheffect effect) {
    if (!port_in_range(port)) {
        RV_LOG_WARN("pccio", "ohaptic on out-of-range port {} (count {})", port, conf_.iport_count);
        return RV_ERR_INVAL;
    }

    switch (effect.type) {
        case RV_HAPTIC_EFFECT_BASIC_RUMBLE:
            // Body actuators: constant strength per side, held for a duration.
            // The host reports RV_ERR_INVAL for an empty slot, which is exactly
            // the code the contract asks for.
            return host_.rumble(port, effect.data.rumble.strength_left,
                                effect.data.rumble.strength_right, effect.data.rumble.duration_ms);

        case RV_HAPTIC_EFFECT_TRIGGER_RUMBLE:
            // Same payload, different actuators (the trigger motors of a
            // DualSense / Steam Deck style pad).
            return host_.rumble_triggers(port, effect.data.rumble.strength_left,
                                         effect.data.rumble.strength_right,
                                         effect.data.rumble.duration_ms);

        case RV_HAPTIC_EFFECT_LEFT_RIGHT_PULSE:
            // DEFERRED. The payload is a TIMED pulse train (on_time_us /
            // off_time_us / repeat_count), i.e. a small effect that has to be
            // stepped across frames — the host currently offers only fire-and-
            // forget rumble with a duration, and there is no effect scheduler to
            // hang the train on. Rejecting is honest; faking it with one long
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

}  // namespace rv_3dmppc
