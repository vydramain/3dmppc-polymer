#include "rv_pconsole/cd/rv_pccd.hpp"

#include "pdk/cd/rv_cd.h"

// --- C contract (pdk/cd/rv_cd.h) ---------------------------------------------
// An rv_cd* handle and the address of an rv_pccd are the same address: which
// concrete class actually lives there is a console construction-time choice
// (rv_pccd_fs), reached here through a virtual call.

extern "C" int64_t rv_cd_asset_open(rv_cd *cd, const char *resname)
{
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->asset_open(resname);
}

extern "C" int64_t rv_cd_asset_size(rv_cd *cd, int64_t handle)
{
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->asset_size(handle);
}

extern "C" int64_t rv_cd_asset_read(rv_cd *cd, int64_t handle, void *baddr, int64_t baddr_size)
{
    return reinterpret_cast<rv_3dmppc::rv_pccd *>(cd)->asset_read(handle, baddr, baddr_size);
}
