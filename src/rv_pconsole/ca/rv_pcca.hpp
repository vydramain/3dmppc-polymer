// The rv_ca contract made abstract: argument validation, the error vocabulary,
// and addresses in and out. Exactly what "sound" means underneath is a choice
// made by whichever concrete class the console picks - the software SPU
// (rv_pcca_sw) or a no-op that still reports the hardware shape the disc
// declared (rv_pcca_null).
#pragma once

#include <cstdint>

#include "pdk/ca/rv_sample.h"
#include "pdk/ca/rv_voice_conf.h"

#include "rv_pconsole/ca/rv_pcstream.hpp"

namespace rv_3dmppc
{

// Format advance() writes: interleaved S16 frames of RV_PCCA_PCM_CHANNELS
// samples at RV_PCCA_PCM_RATE. Every ca implementation produces this.
constexpr int64_t RV_PCCA_PCM_RATE = 44100;
constexpr int64_t RV_PCCA_PCM_CHANNELS = 2;

class rv_pcca
{
public:
    virtual ~rv_pcca() = default;

    rv_pcca(const rv_pcca &) = delete;
    rv_pcca &operator=(const rv_pcca &) = delete;

    virtual int64_t voice_count() = 0;

    virtual int64_t sound_memory_size() = 0;

    virtual int64_t sound_asset_malloc(int64_t size) = 0;

    virtual int64_t sound_asset_write(int64_t addr, const rv_sample *sample) = 0;

    virtual int64_t sound_asset_free(int64_t addr) = 0;

    virtual int64_t voice_setup(const rv_voice_conf *conf) = 0;

    virtual int64_t voice_play(int64_t voice_mask) = 0;

    virtual int64_t voice_stop(int64_t voice_mask) = 0;

    virtual int64_t voice_status(int64_t voice_mask) = 0;

    // Repeat address of the voices in the mask: at the end of a sample under
    // RV_LOOP_FOREVER the read head continues from here. addr must be a frame boundary
    // inside a live region. RV_ERR_INVAL for a bad mask, an unset voice or a bad addr.
    virtual int64_t voice_repeat(int64_t voice_mask, int64_t addr) = 0;

    // Mask of the voices in the mask that reached the end of their sample since the last voice_play.
    // RV_ERR_INVAL for a bad mask or an unset voice.
    virtual int64_t voice_ended(int64_t voice_mask) = 0;

    // Mark address of the voices in the mask; addr 0 clears it. Same addr rules as voice_repeat.
    // Setting or clearing a mark also resets the voices' "has read the mark" flag.
    virtual int64_t voice_mark(int64_t voice_mask, int64_t addr) = 0;

    // Mask of the voices in the mask that have read their mark since it was set.
    // RV_ERR_INVAL for a bad mask or an unset voice.
    virtual int64_t voice_marked(int64_t voice_mask) = 0;

    // Console-side: advance the SPU by `frames` output frames - what the sound device
    // asks for, or one console frame without a device - and write the stereo PCM into
    // `out` (2 * frames interleaved int16). The ONLY thing that moves voices: envelopes,
    // read heads and voice_status() change here and nowhere else. Not reached through
    // the extern "C" block.
    virtual void advance(int16_t *out, int64_t frames) = 0;

    // Stream input level, as voice volume: RV_OK. Holds until the next call and survives
    // the stream's stream_start() and stream_clear() (rv_cd_stream_play / rv_cd_stream_stop).
    virtual int64_t stream_volume(int16_t volume_l, int16_t volume_r) = 0;

    // Console-side, called by the drive; not reached through the extern "C" block.
    // Semantics: rv_pcstream.hpp.
    virtual void stream_start() = 0;                                     // begin a stream
    virtual int64_t stream_push(const int16_t *src, int64_t frames) = 0; // mono frames in, returns frames taken
    virtual void stream_end_input() = 0;                                 // no more data will come
    virtual int64_t stream_reopen_input() = 0;                           // undo stream_end_input(); codes in rv_pcstream.hpp
    virtual void stream_clear() = 0;                                     // stop and empty
    virtual rv_pcstream_report stream_report() = 0;                      // the whole stream state at once

    // Does the memory this controller owns actually exist? Console-side only -
    // not reached through the extern "C" block.
    virtual bool valid() const = 0;

protected:
    rv_pcca() = default;
};

} // namespace rv_3dmppc
