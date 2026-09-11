#include "rv_pconsole/cm/rv_pccm_null.hpp"

#include "pdk/rv_err.h"

namespace rv_3dmppc
{

rv_pccm_null::rv_pccm_null(const rv_pccm_conf &conf)
    : conf_(conf)
{
}

int64_t rv_pccm_null::card_slots()
{
    return conf_.card_slots;
}

int64_t rv_pccm_null::card_slot_size()
{
    return conf_.card_slot_size;
}

int64_t rv_pccm_null::card_size(int64_t /*slot*/)
{
    // Every slot is empty.
    return RV_ERR_NOENT;
}

int64_t rv_pccm_null::card_read(int64_t /*slot*/, void * /*baddr*/, int64_t /*baddr_size*/)
{
    return RV_ERR_NOENT;
}

int64_t rv_pccm_null::card_write(int64_t /*slot*/, const void * /*data*/, int64_t /*data_size*/)
{
    // Not RV_OK: after RV_OK the save must read back, and this card keeps
    // nothing. RV_OK would tell the disc its game was saved.
    return RV_ERR_IO;
}

int64_t rv_pccm_null::card_erase(int64_t /*slot*/)
{
    // Erasing an empty slot succeeds (rv_pccard.hpp, slot_erase).
    return RV_OK;
}

} // namespace rv_3dmppc
