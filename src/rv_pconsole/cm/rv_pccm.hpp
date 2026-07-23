#pragma once

#include "pdk/cm/rv_cm.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc {
class rv_pccm : public rv_cm {
   private:
    rv_pccm_conf conf_;

   public:
    rv_pccm(const rv_pccm_conf conf) : conf_(conf) {}
    ~rv_pccm() = default;

    int64_t card_slots() override;

    int64_t card_slot_size() override;

    int64_t card_size(int64_t slot) override;

    int64_t card_read(int64_t slot, void* baddr, int64_t baddr_size) override;

    int64_t card_write(int64_t slot, const void* data, int64_t data_size) override;

    int64_t card_erase(int64_t slot) override;
};

}  // namespace rv_3dmppc
