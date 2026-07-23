#pragma once

#include <cstdint>

#include "pdk/ca/rv_sample.hpp"
#include "pdk/ca/rv_voice_conf.hpp"

namespace rv_3dmppc {

// Controller Audio — the low-level SPU contract (the "hardware").
//
// Models a PSX-like sound chip: the game uploads sample data into a private pool
// of sound RAM, then configures and triggers a fixed set of voices that play from
// it. This is the LOW-LEVEL layer; the high-level sequencer / music-"mood" layer
// (rv_snd) is DEFERRED and will be built on top of this — see pdk/snd/.
//
// Methods return >= 0 on success, a negative rv_err on failure. A voice mask
// selects voices by bit and uses bits 0..62.
//
// DEFERRED: per-voice pitch/playback rate, reverb, master volume, ADPCM samples.
class rv_ca {
   public:
    virtual ~rv_ca() = default;

    // --- hardware geometry: the game asks, the console answers ---

    // Number of voices this console provides (fits the 0..62 mask bits), and the
    // size of the sound-RAM pool in bytes. Implementation-defined — query in
    // rv_de::disc_initialize and validate the disc's baked assumptions against
    // the answers. The reference console's defaults live in
    // docs/platform/specs.md.
    virtual int64_t voice_count() = 0;
    virtual int64_t sound_memory_size() = 0;

    // --- sound RAM: reserve, fill, release ---

    // Reserve `size` bytes of sound RAM.
    //
    // Returns the address of the region (>= 0), or a negative rv_err:
    //   RV_ERR_INVAL  `size` is not positive
    //   RV_ERR_NOMEM  the pool is full
    //
    // The address is opaque: hand it to sound_asset_write, sound_asset_free and
    // rv_voice_conf::sample_address. It is not a pointer and must not be
    // dereferenced.
    virtual int64_t sound_asset_malloc(int64_t size) = 0;

    // Upload a sample into the region starting at `addr` (from
    // sound_asset_malloc). The sample is self-describing (rv_sample::size), so no
    // separate length argument.
    //
    // Returns RV_OK, or a negative rv_err:
    //   RV_ERR_INVAL  unknown `addr`, null `sample`, or the sample does not fit
    //
    // The bytes are copied during the call; the game may release its own buffer
    // once this returns.
    virtual int64_t sound_asset_write(int64_t addr, const rv_sample* sample) = 0;

    // Release the region at `addr` (the value sound_asset_malloc returned).
    //
    // Returns RV_OK, or a negative rv_err:
    //   RV_ERR_INVAL  unknown `addr`
    //   RV_ERR_BUSY   a voice is still playing from this region
    //
    // Stop the voices first and confirm with voice_status().
    virtual int64_t sound_asset_free(int64_t addr) = 0;

    // --- the voices ---

    // Load a config into the voice(s) named by conf->voice. Arms the voices;
    // nothing is audible until voice_play().
    //
    // Returns RV_OK, or a negative rv_err:
    //   RV_ERR_INVAL  null `conf`, an empty voice mask, or an unknown
    //                 sample_address
    virtual int64_t voice_setup(const rv_voice_conf* conf) = 0;

    // Start / stop every voice selected by `voice_mask`.
    //
    // Returns RV_OK, or a negative rv_err:
    //   RV_ERR_INVAL  the mask names no voice, or a voice was never set up
    virtual int64_t voice_play(int64_t voice_mask) = 0;
    virtual int64_t voice_stop(int64_t voice_mask) = 0;

    // Query the voices in `voice_mask`.
    //
    // Returns a mask of those still playing, bit set = busy (>= 0), or a negative
    // rv_err (RV_ERR_INVAL for an empty mask). Use it to find a free voice, and to
    // confirm a region is idle before sound_asset_free().
    virtual int64_t voice_status(int64_t voice_mask) = 0;
};

}  // namespace rv_3dmppc
