#ifndef RV_PDK_CA_RV_CA_H
#define RV_PDK_CA_RV_CA_H

#include <stdint.h>

#include "pdk/ca/rv_sample.h"
#include "pdk/ca/rv_voice_conf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

typedef struct rv_ca rv_ca;

int64_t rv_ca_voice_count(rv_ca *ca);

int64_t rv_ca_sound_memory_size(rv_ca *ca);
int64_t rv_ca_sound_asset_malloc(rv_ca *ca, int64_t size);
int64_t rv_ca_sound_asset_write(rv_ca *ca, int64_t addr, const rv_sample *sample);
int64_t rv_ca_sound_asset_free(rv_ca *ca, int64_t addr);

int64_t rv_ca_voice_setup(rv_ca *ca, const rv_voice_conf *conf);
int64_t rv_ca_voice_play(rv_ca *ca, int64_t voice_mask);
int64_t rv_ca_voice_stop(rv_ca *ca, int64_t voice_mask);
int64_t rv_ca_voice_status(rv_ca *ca, int64_t voice_mask);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CA_RV_CA_H
