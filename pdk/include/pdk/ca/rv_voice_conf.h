#ifndef RV_PDK_CA_RV_VOICE_CONF_H
#define RV_PDK_CA_RV_VOICE_CONF_H

#include <stdint.h>

#include "pdk/ca/rv_loop.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

struct rv_voice_conf {
    int64_t voice;          // bitmask of the voices this config is loaded into
    enum rv_loop loop_type; // how the sample repeats (see rv_loop)
    int64_t sample_address; // address from rv_ca_sound_asset_malloc

    int16_t ar, dr, sr, rr, sl;         // ADSR rates + sustain level
    int16_t volume, volume_l, volume_r; // overall + per-channel (L/R) volumes
};

/* Plain names: without a typedef, C demands `struct rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as an
   ordinary type. */
typedef struct rv_voice_conf rv_voice_conf;

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CA_RV_VOICE_CONF_H
