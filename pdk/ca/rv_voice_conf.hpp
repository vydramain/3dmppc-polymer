#pragma once

#include <cstdint>

#include "rv_loop.hpp"

namespace rv_3dmppc {

// Everything needed to make one (or several) of the console's voices play a
// sample: which voices, which uploaded sample, how it loops, its envelope, and
// its volumes. rv_ca::voice_setup() loads this into the voice(s) named by
// `voice`; playback is triggered later by rv_ca::voice_play().
//
// Plain POD so it crosses the game/console boundary by value.
//
// DEFERRED: no pitch / playback-rate field yet — a sample plays only at its
// recorded rate. Pitch control comes with a later revision of the audio layer.
struct rv_voice_conf {
    int64_t voice;           // bitmask of the voices this config is loaded into
    rv_loop loop_type;       // how the sample repeats (see rv_loop)
    int64_t sample_address;  // address from rv_ca::sound_asset_malloc

    short ar, dr, sr, rr, sl;        // ADSR rates + sustain level
    short volume, volumeL, volumeR;  // overall + per-channel (L/R) volumes
};

}  // namespace rv_3dmppc
