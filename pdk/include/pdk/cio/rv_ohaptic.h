#ifndef RV_PDK_CIO_RV_OHAPTIC_H
#define RV_PDK_CIO_RV_OHAPTIC_H

#include <stdint.h>

// Kinds of effect. `rv_oheffect.type` carries exactly ONE of these: a tag that
// selects the union member, not a combinable mask.
#define RV_HAPTIC_EFFECT_BASIC_RUMBLE     UINT32_C(0x00000001) // bit 0, payload: rv_oheffect.data.rumble
#define RV_HAPTIC_EFFECT_LEFT_RIGHT_PULSE UINT32_C(0x00000002) // bit 1, payload: rv_oheffect.data.pulse_pattern
#define RV_HAPTIC_EFFECT_TRIGGER_RUMBLE   UINT32_C(0x00000004) // bit 2, payload: rv_oheffect.data.rumble
#define RV_HAPTIC_EFFECT_WAVEFORM         UINT32_C(0x00000008) // bit 3, DEFERRED: payload not defined yet

// Which actuator an effect goes to. This one IS a combinable mask.
#define RV_HAPTIC_TARGET_NONE  UINT32_C(0x00000000) // no actuator
#define RV_HAPTIC_TARGET_LEFT  UINT32_C(0x00000001) // bit 0
#define RV_HAPTIC_TARGET_RIGHT UINT32_C(0x00000002) // bit 1
#define RV_HAPTIC_TARGET_BOTH  (RV_HAPTIC_TARGET_LEFT | RV_HAPTIC_TARGET_RIGHT)

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

struct rv_ohrumble {
    uint16_t strength_left;
    uint16_t strength_right;
    uint16_t duration_ms;
};

struct rv_ohpulse {
    uint32_t target; // rv_ohtarget bitmask

    uint16_t on_time_us;
    uint16_t off_time_us;
    uint16_t repeat_count;
};

struct rv_oheffect {
    uint32_t type; // one rv_ohetype value (a tag selecting `data`, not a combinable mask)

    union {
        struct rv_ohrumble rumble;
        struct rv_ohpulse pulse_pattern;
    } data;
};

/* Plain names: without a typedef, C demands `struct rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as an
   ordinary type. */
typedef struct rv_ohrumble rv_ohrumble;
typedef struct rv_ohpulse rv_ohpulse;
typedef struct rv_oheffect rv_oheffect;

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CIO_RV_OHAPTIC_H
