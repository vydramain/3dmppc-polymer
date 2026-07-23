#pragma once

#include <cstdint>

namespace rv_3dmppc {

// Haptic effect kinds. An effect is exactly ONE of these (the tag in
// rv_oheffect::type); they are not combined.
enum rv_ohetype : uint32_t {
    RV_HAPTIC_EFFECT_BASIC_RUMBLE = 1U << 0,      // payload: rv_oheffect::data.rumble
    RV_HAPTIC_EFFECT_LEFT_RIGHT_PULSE = 1U << 1,  // payload: rv_oheffect::data.pulse_pattern
    RV_HAPTIC_EFFECT_TRIGGER_RUMBLE = 1U << 2,    // payload: rv_oheffect::data.rumble
    RV_HAPTIC_EFFECT_WAVEFORM = 1U << 3,          // DEFERRED: payload not defined yet
};

// Which actuator(s) an effect drives. A real bitmask — BOTH is LEFT | RIGHT.
// NONE is the empty mask, not a bit of its own.
enum rv_ohtarget : uint32_t {
    RV_HAPTIC_TARGET_NONE = 0U,
    RV_HAPTIC_TARGET_LEFT = 1U << 0,
    RV_HAPTIC_TARGET_RIGHT = 1U << 1,
    RV_HAPTIC_TARGET_BOTH = RV_HAPTIC_TARGET_LEFT | RV_HAPTIC_TARGET_RIGHT,
};

// Constant strength per side, held for a fixed duration.
struct rv_ohrumble {
    uint16_t strength_left;
    uint16_t strength_right;
    uint16_t duration_ms;
};

// A repeating on/off pulse train, driven by timing rather than strength.
struct rv_ohpulse {
    uint32_t target;  // rv_ohtarget bitmask

    uint16_t on_time_us;
    uint16_t off_time_us;
    uint16_t repeat_count;
};

// One haptic command handed to rv_cio::ohaptic(). `type` selects which `data`
// member is active (see rv_ohetype).
struct rv_oheffect {
    uint32_t type;  // one rv_ohetype value (a tag selecting `data`, not a combinable mask)

    union {
        rv_ohrumble rumble;
        rv_ohpulse pulse_pattern;
    } data;
};

}  // namespace rv_3dmppc
