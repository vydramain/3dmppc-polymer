// rv_pccl_luajit reload: the entry-reload capability, player build twin, plus
// the player answer for the hook-call ceiling. 3DMPPC_DEVTOOLS is OFF here, so
// both reload overrides refuse without touching Lua, and no ceiling is armed
// around a hook call.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include "pdk/rv_err.h"

namespace rv_3dmppc
{

// No ceiling in a player build: a hook that never returns is the disc's bug to
// ship, not something this runtime interrupts.
int rv_pccl_luajit::hook_insn_ceiling_()
{
    return 0;
}

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
