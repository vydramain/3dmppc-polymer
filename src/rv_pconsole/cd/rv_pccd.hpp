#pragma once

#include "pdk/cd/rv_cd.hpp"

namespace rv_3dmppc {
class rv_pccd : public rv_cd {
   private:
   public:
    rv_pccd() {}
    ~rv_pccd() = default;

    int64_t asset_open(const char* resname) override;

    int64_t asset_size(int64_t handle) override;

    int64_t asset_read(int64_t handle, void* baddr, int64_t baddr_size) override;
};

}  // namespace rv_3dmppc
