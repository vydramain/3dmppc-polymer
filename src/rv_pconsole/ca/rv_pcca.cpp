#include "rv_pconsole/ca/rv_pcca.hpp"

#include "pdk/ca/rv_ca.h"

// --- C contract (pdk/ca/rv_ca.h) ---------------------------------------------
// An rv_ca* handle and the address of an rv_pcca are the same address: which
// concrete class actually lives there is a console construction-time choice
// (rv_pcca_sdl3 or rv_pcca_null), reached here through a virtual call.

extern "C" int64_t rv_ca_voice_count(rv_ca *ca)
{
    return reinterpret_cast<rv_3dmppc::rv_pcca *>(ca)->voice_count();
}

extern "C" int64_t rv_ca_sound_memory_size(rv_ca *ca)
{
    return reinterpret_cast<rv_3dmppc::rv_pcca *>(ca)->sound_memory_size();
}

extern "C" int64_t rv_ca_sound_asset_malloc(rv_ca *ca, int64_t size)
{
    return reinterpret_cast<rv_3dmppc::rv_pcca *>(ca)->sound_asset_malloc(size);
}

extern "C" int64_t rv_ca_sound_asset_write(rv_ca *ca, int64_t addr, const rv_sample *sample)
{
    return reinterpret_cast<rv_3dmppc::rv_pcca *>(ca)->sound_asset_write(addr, sample);
}

extern "C" int64_t rv_ca_sound_asset_free(rv_ca *ca, int64_t addr)
{
    return reinterpret_cast<rv_3dmppc::rv_pcca *>(ca)->sound_asset_free(addr);
}

extern "C" int64_t rv_ca_voice_setup(rv_ca *ca, const rv_voice_conf *conf)
{
    return reinterpret_cast<rv_3dmppc::rv_pcca *>(ca)->voice_setup(conf);
}

extern "C" int64_t rv_ca_voice_play(rv_ca *ca, int64_t voice_mask)
{
    return reinterpret_cast<rv_3dmppc::rv_pcca *>(ca)->voice_play(voice_mask);
}

extern "C" int64_t rv_ca_voice_stop(rv_ca *ca, int64_t voice_mask)
{
    return reinterpret_cast<rv_3dmppc::rv_pcca *>(ca)->voice_stop(voice_mask);
}

extern "C" int64_t rv_ca_voice_status(rv_ca *ca, int64_t voice_mask)
{
    return reinterpret_cast<rv_3dmppc::rv_pcca *>(ca)->voice_status(voice_mask);
}
