// The rv_ca contract made concrete: a private pool of sound RAM, a fixed set of
// voices reading out of it, and a mixer summing them into the PCM the console
// timeline asks for.
//
// The split is the usual one for this tree - this class is the CONTRACT SURFACE
// (argument validation, the error vocabulary, addresses in and out) and owns
// nothing that makes noise; rv_pcmixer owns the voices; rv_pcvoice owns the
// arithmetic. Sound RAM is the same rv_pcpool the video side uses, with a
// different Meta - see rv_pconsole/cv/rv_pcvram.hpp, which is the same idea for
// textures.
//
// Built whenever ca=sw. Needs no audio device: rv_pcca::advance() is called for
// what the sound device asks for or, without one, by the frame loop.
#pragma once

#include <cstdint>

#include "pdk/ca/rv_sample.h"
#include "pdk/ca/rv_voice_conf.h"

#include "rv_pconsole/mem/rv_pcpool.hpp"
#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/ca/rv_pcmixer.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"
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

class rv_pcca_sw final : public rv_pcca
{
private:
    rv_pcca_conf conf_;

    // DECLARATION ORDER IS LOAD-BEARING: the voices inside mixer_ hold pointers
    // into sram_, so sound RAM must be constructed first and destroyed last.
    rv_pcpool<rv_pcca_meta> sram_;
    rv_pcmixer mixer_;

    // Common guard for every mask-taking entry point: a mask must name at least
    // one voice and must name only voices this console has. Returns RV_OK or
    // RV_ERR_INVAL.
    int64_t validate_mask(int64_t voice_mask) const;

    // A sound address resolved to the live region holding it. `start` is the
    // identity voices remember; `offset` counts frames from that start.
    struct rv_pcca_place {
        int64_t start = 0;
        const uint8_t *data = nullptr;
        int64_t frames = 0;
        int64_t offset = 0;
    };

    // RV_ERR_INVAL when `addr` is not inside a live region or is not on a frame
    // boundary of it. Address 0 is never a region.
    int64_t locate(int64_t addr, rv_pcca_place &place) const;

public:
    explicit rv_pcca_sw(const rv_pcca_conf &conf);

    rv_pcca_sw(const rv_pcca_sw &) = delete;
    rv_pcca_sw &operator=(const rv_pcca_sw &) = delete;

    // The peak host bytes this class allocates for `budget`: sram_
    // (sound_memory_size) + the pool's block bookkeeping + mixer_'s voices
    // (one rv_pcvoice each), its fixed render accumulator and its stream ring
    // (RV_PCSTREAM_RING_FRAMES int16). Overflow fails with RV_ERR_INVAL and `error` set;
    // every field it reads was already validated as non-negative by rv_pboot_check_budget()
    // before any evaluate().
    static int evaluate(const rv_pdklib::rv_manifest_budget &budget, int64_t &bytes, std::string &error);

    int64_t voice_count() override;

    int64_t sound_memory_size() override;

    int64_t sound_asset_malloc(int64_t size) override;

    int64_t sound_asset_write(int64_t addr, const rv_sample *sample) override;

    int64_t sound_asset_free(int64_t addr) override;

    int64_t voice_setup(const rv_voice_conf *conf) override;

    int64_t voice_play(int64_t voice_mask) override;

    int64_t voice_stop(int64_t voice_mask) override;

    int64_t voice_status(int64_t voice_mask) override;

    int64_t voice_repeat(int64_t voice_mask, int64_t addr) override;

    int64_t voice_ended(int64_t voice_mask) override;

    int64_t voice_mark(int64_t voice_mask, int64_t addr) override;

    int64_t voice_marked(int64_t voice_mask) override;

    void advance(int16_t *out, int64_t frames) override;

    int64_t stream_volume(int16_t volume_l, int16_t volume_r) override;

    void stream_start() override;

    int64_t stream_push(const int16_t *src, int64_t frames) override;

    void stream_end_input() override;

    void stream_clear() override;

    rv_pcstream_report stream_report() override;

    // Does the memory this controller owns actually exist? False when the
    // sound RAM pool failed to reserve.
    bool valid() const override
    {
        return sram_.valid();
    }
};

} // namespace rv_3dmppc
