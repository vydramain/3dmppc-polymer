#pragma once

#include "rv_sample.hpp"
#include "rv_voice_conf.hpp"

namespace rv_3dmppc {

// Controller Audio — the low-level SPU contract (the "hardware").
//
// Models a PSX-like sound chip: the game uploads sample data into a private pool
// of sound RAM, then configures and triggers a fixed set of voices that play from
// it. This is the LOW-LEVEL layer; the high-level sequencer / music-"mood" layer
// (rv_snd) is DEFERRED and will be built on top of this — see pdk/snd/.
//
// Every method returns an int per the rv_err convention: >= 0 on success, a
// negative rv_err on failure. The console subclasses this; a game only ever holds
// an rv_ca* handed out by rv_pdko.
//
// DEFERRED: per-voice pitch/playback rate, reverb, master volume, ADPCM samples.
class rv_ca {
   public:
    virtual ~rv_ca() = default;

    // Bring the audio hardware up. RV_OK, or a negative rv_err (e.g. no device).
    virtual int init() = 0;

    // --- sound RAM: reserve, fill, release ---
    // Reserve `size` bytes of sound RAM. Returns the region's address (>= 0), or a
    // negative rv_err (RV_ERR_NOMEM when the pool is full).
    virtual int malloc(long size) = 0;
    // Upload a sample into the region starting at `addr` (from malloc). The sample
    // is self-describing (rv_sample::size). Returns RV_OK or a negative rv_err.
    virtual int write(int addr, const rv_sample* sample) = 0;
    // Release the region at `addr` (the value malloc returned). Returns RV_OK or a
    // negative rv_err. No voice may still be playing this region when it is freed.
    virtual int free(int addr) = 0;

    // --- the voices ---
    // Load a config into the voice(s) named by conf->voice. RV_OK or negative.
    virtual int voice_setup(const rv_voice_conf* conf) = 0;
    // Start / stop the voices selected by `voice_mask`. RV_OK or negative.
    virtual int voice_play(int voice_mask) = 0;
    virtual int voice_stop(int voice_mask) = 0;
    // Query the voices in `voice_mask`. Returns a bitmask of those still playing
    // (bit set = busy), or a negative rv_err. Use it to find a free voice.
    virtual int voice_status(int voice_mask) = 0;
};

}  // namespace rv_3dmppc
