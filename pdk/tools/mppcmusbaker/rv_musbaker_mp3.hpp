// mppcmusbaker: decoding an MP3 into the console's raw S16LE mono samples.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pdk/rv_err.h"

// Decodes `input` (MPEG Layer III, 44100 Hz, mono or stereo) into the bytes
// load_wav_pcm produces: headerless S16LE mono, stereo averaged to mono. A file
// at another rate is refused, not resampled; a damaged or truncated one too.
// On failure `error` holds the reason.
rv_err load_mp3_pcm(const std::string &input, std::vector<uint8_t> *out, std::string *error);
