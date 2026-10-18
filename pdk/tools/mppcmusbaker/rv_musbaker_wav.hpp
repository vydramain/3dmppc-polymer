// mppcmusbaker: reading a RIFF/WAVE file into the console's raw S16LE mono samples.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pdk/rv_err.h"

// Reads `input` (RIFF/WAVE, PCM, 16-bit or 24-bit, 44100 Hz, mono or stereo) and fills
// `out` with the console's sound bytes: headerless S16LE mono, one sample per
// two bytes, nothing else - what rv_ca::sound_asset_write expects. Stereo is
// downmixed to mono by averaging the two channels. 24-bit is rounded to 16-bit.
// Anything else (other rates, other bit depths, float, EXTENSIBLE, more than two
// channels) is refused. On failure `error`
// holds the reason; the caller decides how to name the tool.
rv_err load_wav_pcm(const std::string &input, std::vector<uint8_t> *out, std::string *error);
