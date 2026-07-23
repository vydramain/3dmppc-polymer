#pragma once

#include "pdk/ca/rv_ca.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc {

class rv_pcca : public rv_ca {
   private:
    rv_pcca_conf conf_;

   public:
    rv_pcca(const rv_pcca_conf conf) : conf_(conf) {}
    ~rv_pcca() = default;

    int64_t voice_count() override;

    int64_t sound_memory_size() override;

    int64_t sound_asset_malloc(int64_t size) override;

    int64_t sound_asset_write(int64_t addr, const rv_sample* sample) override;

    int64_t sound_asset_free(int64_t addr) override;

    int64_t voice_setup(const rv_voice_conf* conf) override;

    int64_t voice_play(int64_t voice_mask) override;

    int64_t voice_stop(int64_t voice_mask) override;

    int64_t voice_status(int64_t voice_mask) override;
};

}  // namespace rv_3dmppc
