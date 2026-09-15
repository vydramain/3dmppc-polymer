// The rv_cm contract made abstract. Where a save's bytes live is the concrete
// card's business (rv_pccm_posix or rv_pccm_null).
#pragma once

#include <cstdint>

namespace rv_3dmppc
{

class rv_pccm
{
public:
    virtual ~rv_pccm() = default;

    rv_pccm(const rv_pccm &) = delete;
    rv_pccm &operator=(const rv_pccm &) = delete;

    virtual int64_t card_slots() = 0;

    virtual int64_t card_slot_size() = 0;

    virtual int64_t card_size(int64_t slot) = 0;

    virtual int64_t card_read(int64_t slot, void *baddr, int64_t baddr_size) = 0;

    virtual int64_t card_write(int64_t slot, const void *data, int64_t data_size) = 0;

    virtual int64_t card_erase(int64_t slot) = 0;

    // Console-side only - not reached through the extern "C" block.
    virtual bool valid() const = 0;

protected:
    rv_pccm() = default;
};

} // namespace rv_3dmppc
