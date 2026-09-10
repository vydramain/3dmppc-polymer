#include "rv_pconsole/cio/rv_pccio_null.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

rv_pccio_null::rv_pccio_null(const rv_pccio_conf &conf)
    : conf_(conf)
{
    RV_LOG_INFO("pccio", "input off ({} port(s) declared, every port empty)", conf_.iport_count);
}

int64_t rv_pccio_null::iport_count()
{
    return conf_.iport_count;
}

uint64_t rv_pccio_null::iport_abilities(int64_t)
{
    return 0;
}

rv_imouse rv_pccio_null::imouse()
{
    return rv_imouse{};
}

rv_istate rv_pccio_null::iport_state(int64_t)
{
    return rv_istate{};
}

// Every slot of this machine is empty, so every port index — whether in-range
// or not — is an inactive slot. RV_ERR_INVAL is the contract's answer for
// trying to activate an empty slot.
int64_t rv_pccio_null::ohaptic(int64_t, rv_oheffect)
{
    return RV_ERR_INVAL;
}

} // namespace rv_3dmppc
