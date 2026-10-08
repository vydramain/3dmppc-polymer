// mppcbaker: reading a RIFF/WAVE file into the console's raw S16LE mono samples.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rv_baker_encode.hpp" // baker_error

// Reads `input` (RIFF/WAVE, PCM, 16-bit, 44100 Hz, mono or stereo) and fills
// `out` with the console's sound bytes: headerless S16LE mono, one sample per
// two bytes, nothing else - what rv_ca::sound_asset_write expects. Stereo is
// downmixed to mono by averaging the two channels. Anything else (other rates,
// bit depths, float, more than two channels) is refused.
rv_err load_wav_pcm(const std::string &input, std::vector<uint8_t> *out, baker_error *error);
