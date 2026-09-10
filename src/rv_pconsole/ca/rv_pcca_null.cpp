#include "rv_pconsole/ca/rv_pcca_null.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

rv_pcca_null::rv_pcca_null(const rv_pcca_conf &conf)
    : conf_(conf)
{
    RV_LOG_INFO("pcca", "audio off ({} voice(s), {} byte(s) declared, no-op)", conf_.voice_count,
        conf_.sound_memory_size);
}

int64_t rv_pcca_null::voice_count()
{
    // The DISC's declared count, unchanged — a no-op console still reports the
    // hardware shape a real one would have had.
    return conf_.voice_count;
}

int64_t rv_pcca_null::sound_memory_size()
{
    return conf_.sound_memory_size;
}

int64_t rv_pcca_null::sound_asset_malloc(int64_t /*size*/)
{
    // A fixed, positive, fake address: nothing is allocated, and repeats are
    // fine — no pool exists for this to collide against.
    constexpr int64_t RV_PCCA_NULL_FAKE_ADDR = 16;
    return RV_PCCA_NULL_FAKE_ADDR;
}

int64_t rv_pcca_null::sound_asset_write(int64_t /*addr*/, const rv_sample * /*sample*/)
{
    return RV_OK;
}

int64_t rv_pcca_null::sound_asset_free(int64_t /*addr*/)
{
    return RV_OK;
}

int64_t rv_pcca_null::voice_setup(const rv_voice_conf * /*conf*/)
{
    return RV_OK;
}

int64_t rv_pcca_null::voice_play(int64_t /*voice_mask*/)
{
    return RV_OK;
}

int64_t rv_pcca_null::voice_stop(int64_t /*voice_mask*/)
{
    return RV_OK;
}

int64_t rv_pcca_null::voice_status(int64_t /*voice_mask*/)
{
    // No voice is ever busy in a no-op console.
    return 0;
}

} // namespace rv_3dmppc
