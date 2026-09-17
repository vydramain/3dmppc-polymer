// rv_pccl_luajit reload: the entry-reload capability, player build twin.
// 3DMPPC_DEVTOOLS is OFF, so both overrides refuse without touching Lua.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include "pdk/rv_err.h"

namespace rv_3dmppc
{

int64_t rv_pccl_luajit::script_reload_entry(const void * /*bytecode*/, int64_t /*size*/,
    const char * /*name*/, rv_pccl_reload_report &report)
{
    report.phase = "not_reloadable";
    report.message = "this console was built without the development runtime; "
        "rebuild with -D3DMPPC_DEVTOOLS=ON to reload the entry chunk";
    report.effects_possible = false;
    return RV_ERR_INVAL;
}

int64_t rv_pccl_luajit::script_reload_entry_from_drive(rv_pccl_reload_report &report)
{
    report.phase = "not_reloadable";
    report.message = "this console was built without the development runtime; "
        "rebuild with -D3DMPPC_DEVTOOLS=ON to reload the entry chunk";
    report.effects_possible = false;
    return RV_ERR_INVAL;
}

} // namespace rv_3dmppc
