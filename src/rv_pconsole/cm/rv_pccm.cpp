#include "rv_pconsole/cm/rv_pccm.hpp"

#include "pdk/cm/rv_cm.h"

// --- C contract (pdk/cm/rv_cm.h) ---------------------------------------------
// An rv_cm* handle and the address of an rv_pccm are the same address: which
// concrete class actually lives there is a console construction-time choice
// (rv_pccm_posix), reached here through a virtual call.

extern "C" int64_t rv_cm_card_slots(rv_cm *cm)
{
    return reinterpret_cast<rv_3dmppc::rv_pccm *>(cm)->card_slots();
}

extern "C" int64_t rv_cm_card_slot_size(rv_cm *cm)
{
    return reinterpret_cast<rv_3dmppc::rv_pccm *>(cm)->card_slot_size();
}

extern "C" int64_t rv_cm_card_size(rv_cm *cm, int64_t slot)
{
    return reinterpret_cast<rv_3dmppc::rv_pccm *>(cm)->card_size(slot);
}

extern "C" int64_t rv_cm_card_read(rv_cm *cm, int64_t slot, void *baddr, int64_t baddr_size)
{
    return reinterpret_cast<rv_3dmppc::rv_pccm *>(cm)->card_read(slot, baddr, baddr_size);
}

extern "C" int64_t rv_cm_card_write(rv_cm *cm, int64_t slot, const void *data, int64_t data_size)
{
    return reinterpret_cast<rv_3dmppc::rv_pccm *>(cm)->card_write(slot, data, data_size);
}

extern "C" int64_t rv_cm_card_erase(rv_cm *cm, int64_t slot)
{
    return reinterpret_cast<rv_3dmppc::rv_pccm *>(cm)->card_erase(slot);
}
