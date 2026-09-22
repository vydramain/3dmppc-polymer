// The hook-to-Lua forwarding every script disc needs lives in pdklib
// (pdklib/rv_dscript/rv_dscript.hpp); the entry points every disc must plant
// are pdk's, and stay written here.
#include "pdklib/rv_dscript/rv_dscript.hpp"

RV_MPPC_DISC_LUA_DEF("example-lua")
RV_MPPC_DISC_ENTRY_DEF(rv_dscript_disc_)
