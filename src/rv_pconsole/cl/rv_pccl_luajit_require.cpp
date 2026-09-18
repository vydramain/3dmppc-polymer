// The console's own require("name"): reads name.lua/name.luac off the drive,
// runs it once, and hands every caller the same table. Not the stock package
// library, which stays closed - see rv_pccl_luajit.cpp's bootstrap comment.
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "lua.hpp"

namespace rv_3dmppc
{
namespace
{

// Undoes the "being loaded" marker for `name` without raising, so every
// failure path below can clear it and then error freely.
void clear_marker(lua_State *L, int loaded_idx, const char *name)
{
    lua_pushstring(L, name);
    lua_pushnil(L);
    lua_rawset(L, loaded_idx);
}

} // namespace

// The same name/extension rules require_ has always used: empty, over 200
// bytes, or containing '.', '/', '\\' is not a module name (1); the entry's
// own extension picks .lua or .luac for every module (2 when it has neither).
// Never raises: require_'s callers still need lua error text of their own,
// and the dev reload path calls this outside any pcall at all.
int rv_pccl_luajit::module_asset_(const char *name, char *out, std::size_t cap) const
{
    const std::size_t len = std::strlen(name);
    bool bad = len == 0 || len > 200;
    for (std::size_t i = 0; !bad && i < len; ++i) {
        const char c = name[i];
        if (c == '.' || c == '/' || c == '\\') {
            bad = true;
        }
    }
    if (bad) {
        return 1;
    }

    const char *ext = nullptr;
    if (conf_.script_entry.ends_with(".luac")) {
        ext = ".luac";
    } else if (conf_.script_entry.ends_with(".lua")) {
        ext = ".lua";
    } else {
        return 2;
    }

    std::snprintf(out, cap, "%s%s", name, ext);
    return 0;
}

// No std::string/std::vector lives in this frame: luaL_error/lua_error
// longjmp out of it, and a C++ object with a destructor must not be alive
// when that happens. The module name is bounded and copied into a fixed
// buffer; the asset bytes live in a lua_newuserdata block the VM itself owns.
int rv_pccl_luajit::require_(lua_State *L)
{
    void *ud = nullptr;
    lua_getallocf(L, &ud);
    rv_pccl_luajit *self = static_cast<rv_pccl_luajit *>(ud);

    const char *name = luaL_checkstring(L, 1);
    char asset[256];
    const int asset_code = self->module_asset_(name, asset, sizeof asset);
    if (asset_code == 1) {
        return luaL_error(L,
            "require('%s'): a module name is a file name with no extension and no directory", name);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, self->loaded_ref_);
    const int loaded_idx = lua_gettop(L);
    lua_pushstring(L, name);
    lua_rawget(L, loaded_idx);
    if (lua_istable(L, -1)) {
        return 1; // the same table every caller gets
    }
    if (lua_isboolean(L, -1)) {
        return luaL_error(L, "require('%s'): loop - the module is already being loaded", name);
    }
    lua_pop(L, 1); // nil: not loaded yet

    lua_pushstring(L, name);
    lua_pushboolean(L, 0);
    lua_rawset(L, loaded_idx); // loaded[name] = false, the loading marker

    // The entry's own extension decides the archive/--unpacked question for
    // every module, not just the entry itself.
    if (asset_code == 2) {
        clear_marker(L, loaded_idx, name);
        return luaL_error(L, "require('%s'): the entry script '%s' is neither .lua nor .luac", name,
            self->conf_.script_entry.c_str());
    }

    const int64_t handle = self->cd_.asset_open(asset);
    const int64_t size = handle >= 0 ? self->cd_.asset_size(handle) : -1;
    if (handle < 0 || size <= 0) {
        clear_marker(L, loaded_idx, name);
        return luaL_error(L, "require('%s'): no module '%s' on the disc", name, asset);
    }

    void *bytes = lua_newuserdata(L, static_cast<std::size_t>(size)); // kept alive on the stack
    const int64_t read = self->cd_.asset_read(handle, bytes, size);
    if (read != size) {
        clear_marker(L, loaded_idx, name);
        return luaL_error(L, "require('%s'): no module '%s' on the disc", name, asset);
    }

    if (luaL_loadbuffer(L, static_cast<const char *>(bytes), static_cast<std::size_t>(size), asset) != 0) {
        clear_marker(L, loaded_idx, name);
        return lua_error(L); // loadbuffer's own message, unchanged
    }
    lua_remove(L, -2); // the userdata; the compiled chunk stays on top

    if (lua_pcall(L, 0, 1, 0) != 0) {
        clear_marker(L, loaded_idx, name);
        return lua_error(L); // the module's own error, unchanged
    }

    if (!lua_istable(L, -1)) {
        clear_marker(L, loaded_idx, name);
        return luaL_error(L, "require('%s'): the module did not return a table", name);
    }

    lua_pushstring(L, name);
    lua_pushvalue(L, -2);
    lua_rawset(L, loaded_idx); // loaded[name] = the module's table
    return 1;
}

} // namespace rv_3dmppc
