// Group B of the rv_cl contract: raw stack access. Nothing here does more than
// wrap one lua_* call and translate its answer into an rv_err / RV_CL_TYPE_*.
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include <cstring>

#include "lua.hpp"

#include "pdk/cl/rv_cl.h"
#include "pdk/rv_err.h"

namespace rv_3dmppc
{
// --- group B: raw stack access, direct lua_* wrappers ---
int64_t rv_pccl_luajit::stack_push_nil()
{
    lua_pushnil(L_);
    return RV_OK;
}
int64_t rv_pccl_luajit::stack_push_boolean(bool value)
{
    lua_pushboolean(L_, value);
    return RV_OK;
}
int64_t rv_pccl_luajit::stack_push_integer(int64_t value)
{
    lua_pushinteger(L_, static_cast<lua_Integer>(value));
    return RV_OK;
}
int64_t rv_pccl_luajit::stack_push_number(double value)
{
    lua_pushnumber(L_, static_cast<lua_Number>(value));
    return RV_OK;
}
int64_t rv_pccl_luajit::stack_push_string(const char *text, int64_t length)
{
    if (text == nullptr || length < 0) {
        return RV_ERR_INVAL;
    }
    lua_pushlstring(L_, text, static_cast<size_t>(length));
    return RV_OK;
}
int64_t rv_pccl_luajit::stack_push_pointer(void *p)
{
    lua_pushlightuserdata(L_, p);
    return RV_OK;
}
int64_t rv_pccl_luajit::stack_drop(int64_t count)
{
    if (count < 0 || count > lua_gettop(L_)) {
        return RV_ERR_INVAL;
    }
    lua_pop(L_, static_cast<int>(count));
    return RV_OK;
}
int64_t rv_pccl_luajit::stack_count()
{
    return static_cast<int64_t>(lua_gettop(L_));
}
int64_t rv_pccl_luajit::value_type(int64_t index)
{
    switch (lua_type(L_, static_cast<int>(index))) {
    case LUA_TNIL:
        return RV_CL_TYPE_NIL;
    case LUA_TBOOLEAN:
        return RV_CL_TYPE_BOOLEAN;
    case LUA_TNUMBER:
        return RV_CL_TYPE_NUMBER;
    case LUA_TSTRING:
        return RV_CL_TYPE_STRING;
    case LUA_TFUNCTION:
        return RV_CL_TYPE_FUNCTION;
    case LUA_TTABLE:
        return RV_CL_TYPE_TABLE;
    case LUA_TNONE:
        return RV_ERR_INVAL; // no value at that index
    default:
        return RV_CL_TYPE_OTHER;
    }
}
int64_t rv_pccl_luajit::value_boolean(int64_t index, bool *out)
{
    if (out == nullptr) {
        return RV_ERR_INVAL;
    }
    if (lua_type(L_, static_cast<int>(index)) != LUA_TBOOLEAN) {
        return RV_ERR_INVAL;
    }
    *out = lua_toboolean(L_, static_cast<int>(index)) != 0;
    return RV_OK;
}
int64_t rv_pccl_luajit::value_integer(int64_t index, int64_t *out)
{
    if (out == nullptr) {
        return RV_ERR_INVAL;
    }
    int isnum = 0;
    const lua_Integer v = lua_tointegerx(L_, static_cast<int>(index), &isnum);
    if (!isnum) {
        return RV_ERR_INVAL;
    }
    *out = static_cast<int64_t>(v);
    return RV_OK;
}
int64_t rv_pccl_luajit::value_number(int64_t index, double *out)
{
    if (out == nullptr) {
        return RV_ERR_INVAL;
    }
    int isnum = 0;
    const lua_Number v = lua_tonumberx(L_, static_cast<int>(index), &isnum);
    if (!isnum) {
        return RV_ERR_INVAL;
    }
    *out = static_cast<double>(v);
    return RV_OK;
}
// Typed BEFORE lua_tolstring: on a number it would convert it to a string IN
// PLACE on the stack. LUA_TSTRING-only sidesteps that.
int64_t rv_pccl_luajit::value_string(int64_t index, char *baddr, int64_t baddr_size)
{
    if (baddr_size < 0) {
        return RV_ERR_INVAL;
    }
    if (lua_type(L_, static_cast<int>(index)) != LUA_TSTRING) {
        return RV_ERR_INVAL;
    }
    size_t len = 0;
    const char *s = lua_tolstring(L_, static_cast<int>(index), &len);
    if (baddr != nullptr) { // the pointer is the collector's; copy, never hand it out
        const int64_t n = baddr_size < static_cast<int64_t>(len) ? baddr_size : static_cast<int64_t>(len);
        std::memcpy(baddr, s, static_cast<size_t>(n));
    }
    return static_cast<int64_t>(len);
}

} // namespace rv_3dmppc
