#pragma once

#include <cstdint>

#include "pdk/ca/rv_loop.hpp"

namespace rv_pdk
{

struct rv_voice_conf {
	int64_t voice;          // bitmask of the voices this config is loaded into
	rv_loop loop_type;      // how the sample repeats (see rv_loop)
	int64_t sample_address; // address from rv_ca::sound_asset_malloc

	int16_t ar, dr, sr, rr, sl;         // ADSR rates + sustain level
	int16_t volume, volume_l, volume_r; // overall + per-channel (L/R) volumes
};

} // namespace rv_pdk
