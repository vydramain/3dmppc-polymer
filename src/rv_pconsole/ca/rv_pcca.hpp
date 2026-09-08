// The rv_ca contract made concrete: a private pool of sound RAM, a fixed set of
// voices reading out of it, and a mixer summing them for the host's device.
//
// The split is the usual one for this tree — this class is the CONTRACT SURFACE
// (argument validation, the error vocabulary, addresses in and out) and owns
// nothing that makes noise; rv_pcmixer owns the voices and the thread boundary;
// rv_pcvoice owns the arithmetic. Sound RAM is the same rv_pcpool the video side
// uses, with a different Meta — see rv_pconsole/cv/rv_pcvram.hpp, which is the
// same idea for textures.
#pragma once

#include <cstdint>
#include <optional>

#include "pdk/ca/rv_sample.h"
#include "pdk/ca/rv_voice_conf.h"

#include "rv_pmem/rv_pcpool.hpp"
#include "rv_pconsole/ca/rv_pcmixer.hpp"
#include "rv_pconsole/rv_pchost.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

// What a sound-RAM region remembers about its last upload. Only the length is
// needed: format is fixed by the console (raw S16LE mono at RV_PCA_SAMPLE_RATE)
// and is exactly why rv_sample carries no format fields.
struct rv_pcca_meta {
    bool written = false;
    int64_t frames = 0; // uploaded frames, i.e. bytes / RV_PCA_FRAME_BYTES
};

class rv_pcca
{
private:
    rv_pcca_conf conf_;

    // Borrowed: the host owns the audio device and outlives every controller.
    rv_pchost &host_;

    // DECLARATION ORDER IS LOAD-BEARING: the voices inside mixer_ hold pointers
    // into sram_, so sound RAM must be constructed first and destroyed last.
    // Both are empty (nullopt) whenever audio is off — a console with
    // --no_audio or a refused device allocates neither.
    std::optional<rv_pcpool<rv_pcca_meta>> sram_;
    std::optional<rv_pcmixer> mixer_;

    // Is audio on at all? False for --no-audio and for a device the host
    // refused to open; either way every one of the nine calls below becomes a
    // no-op that always succeeds — see the constructor.
    bool sounding_ = false;

    // Common guard for every mask-taking entry point: a mask must name at least
    // one voice and must name only voices this console has. Returns RV_OK or
    // RV_ERR_INVAL.
    int64_t validate_mask(int64_t voice_mask) const;

public:
    rv_pcca(const rv_pcca_conf &conf, rv_pchost &host);

    // Detaches the mixer from the host BEFORE it stops existing. Does not
    // close the device — that belongs to the host's lifetime (stage C), not
    // to this disc's.
    ~rv_pcca();

    rv_pcca(const rv_pcca &) = delete;
    rv_pcca &operator=(const rv_pcca &) = delete;

    int64_t voice_count();

    int64_t sound_memory_size();

    int64_t sound_asset_malloc(int64_t size);

    int64_t sound_asset_write(int64_t addr, const rv_sample *sample);

    int64_t sound_asset_free(int64_t addr);

    int64_t voice_setup(const rv_voice_conf *conf);

    int64_t voice_play(int64_t voice_mask);

    int64_t voice_stop(int64_t voice_mask);

    int64_t voice_status(int64_t voice_mask);

    // Does the memory this controller owns actually exist? True when audio is
    // off (sram_ is empty BY DESIGN, not a failure); false only when audio is
    // on and the sound RAM pool failed to reserve.
    bool valid() const
    {
        return !sounding_ || sram_->valid();
    }
};

} // namespace rv_3dmppc
