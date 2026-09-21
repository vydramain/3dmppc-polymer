// The hook-to-Lua forwarding every script disc needs now lives in pdklib
// (pdklib/rv_dscript/rv_dscript.hpp) - this file only asks for it.
#include "pdklib/rv_dscript/rv_dscript.hpp"

RV_MPPC_DISC_LUA_DEF("example-lua")
