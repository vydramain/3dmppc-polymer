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

// Repeat, end and mark events. Addresses are byte offsets on a 2-byte frame boundary
// inside a live sound region (addr may point inside a region). Bad mask, unset voice or bad addr: RV_ERR_INVAL.
// repeat: at the end of a sample under RV_LOOP_FOREVER the voice continues from addr,
// sample to sample, also into another region. voice_play and voice_setup reset it to the sample start.
// ended: on success, the mask (>= 0) of voices that reached the end of their sample at least once
// since the last voice_play. RV_ERR_INVAL for a bad mask or an unset voice.
// mark: one per voice; its flag is raised when the voice reads the frame at addr (also when it
// arrives there by repeat). addr 0 clears the mark. Setting or clearing resets the flag; voice_setup clears it.
// marked: on success, the mask (>= 0) of voices that have read their mark since it was set.
// RV_ERR_INVAL for a bad mask or an unset voice.
// sound_asset_free: RV_ERR_BUSY while a region is the current, start or repeat target of a busy voice,
// or the target of any voice's mark. Move repeat and marks first, then free.
// Timing: the disc sees ended and marked with the sound device block granularity, and reacts within
// one game frame plus one block (about 60 ms on web). A stream chunk the disc pages in on a mark
// must be longer than that, or the voice catches up with unfilled data.
int64_t rv_ca_voice_repeat(rv_ca *ca, int64_t voice_mask, int64_t addr);
int64_t rv_ca_voice_ended(rv_ca *ca, int64_t voice_mask);
int64_t rv_ca_voice_mark(rv_ca *ca, int64_t voice_mask, int64_t addr);
int64_t rv_ca_voice_marked(rv_ca *ca, int64_t voice_mask);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CA_RV_CA_H
