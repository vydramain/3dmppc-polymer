// The rv_ca contract impersonated: no pool, no mixer, no device — but a no-op
// console still describes the hardware shape a real one would have had. Ca has
// no return channel with an invariant to keep true (unlike, say, a disc that
// must see its own writes reflected back), so it may impersonate freely.
#pragma once

#include <cstdint>

#include "pdk/ca/rv_sample.h"
#include "pdk/ca/rv_voice_conf.h"

#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

class rv_pcca_null final : public rv_pcca
{
private:
    rv_pcca_conf conf_;

public:
    explicit rv_pcca_null(const rv_pcca_conf &conf);

    int64_t voice_count() override;

    int64_t sound_memory_size() override;

    int64_t sound_asset_malloc(int64_t size) override;

    int64_t sound_asset_write(int64_t addr, const rv_sample *sample) override;

    int64_t sound_asset_free(int64_t addr) override;

    int64_t voice_setup(const rv_voice_conf *conf) override;

    int64_t voice_play(int64_t voice_mask) override;

    int64_t voice_stop(int64_t voice_mask) override;

    int64_t voice_status(int64_t voice_mask) override;

    bool valid() const override
    {
        return true;
    }
};

} // namespace rv_3dmppc
