// The rv_cm contract with a card that keeps nothing: the declared geometry,
// every slot empty, no save ever accepted.
#pragma once

#include <cstdint>

#include "rv_pconsole/cm/rv_pccm.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

class rv_pccm_null final : public rv_pccm
{
private:
    rv_pccm_conf conf_;

public:
    explicit rv_pccm_null(const rv_pccm_conf &conf);

    int64_t card_slots() override;

    int64_t card_slot_size() override;

    int64_t card_size(int64_t slot) override;

    int64_t card_read(int64_t slot, void *baddr, int64_t baddr_size) override;

    int64_t card_write(int64_t slot, const void *data, int64_t data_size) override;

    int64_t card_erase(int64_t slot) override;

    bool valid() const override { return true; }
};

} // namespace rv_3dmppc
