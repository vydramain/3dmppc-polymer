// The rv_ca contract made abstract: argument validation, the error vocabulary,
// and addresses in and out. Exactly what "sound" means underneath is a choice
// made by whichever concrete class the console picks — a real mixer over a
// real device (rv_pcca_sdl3) or a no-op that still reports the hardware shape
// the disc declared (rv_pcca_null).
#pragma once

#include <cstdint>

#include "pdk/ca/rv_sample.h"
#include "pdk/ca/rv_voice_conf.h"

namespace rv_3dmppc
{

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

    // Does the memory this controller owns actually exist? Console-side only —
    // not reached through the extern "C" block.
    virtual bool valid() const = 0;

protected:
    rv_pcca() = default;
};

} // namespace rv_3dmppc
