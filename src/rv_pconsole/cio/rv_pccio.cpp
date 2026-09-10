#include "rv_pconsole/cio/rv_pccio.hpp"

#include "pdk/cio/rv_cio.h"

// --- C contract (pdk/cio/rv_cio.h) -------------------------------------------
// An rv_cio* handle and the address of an rv_pccio are the same address: which
// concrete class actually lives there is a console construction-time choice
// (rv_pccio_sdl3 or rv_pccio_null), reached here through a virtual call.

extern "C" int64_t rv_cio_iport_count(rv_cio *cio)
{
    return reinterpret_cast<rv_3dmppc::rv_pccio *>(cio)->iport_count();
}

extern "C" rv_istate rv_cio_iport_state(rv_cio *cio, int64_t port)
{
    return reinterpret_cast<rv_3dmppc::rv_pccio *>(cio)->iport_state(port);
}

extern "C" uint64_t rv_cio_iport_abilities(rv_cio *cio, int64_t port)
{
    return reinterpret_cast<rv_3dmppc::rv_pccio *>(cio)->iport_abilities(port);
}

extern "C" rv_imouse rv_cio_imouse(rv_cio *cio)
{
    return reinterpret_cast<rv_3dmppc::rv_pccio *>(cio)->imouse();
}

extern "C" int64_t rv_cio_ohaptic(rv_cio *cio, int64_t port, rv_oheffect effect)
{
    return reinterpret_cast<rv_3dmppc::rv_pccio *>(cio)->ohaptic(port, effect);
}
