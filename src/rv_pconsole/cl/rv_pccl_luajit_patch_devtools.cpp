// Patching a chunk's tables in place - the entry on `reload entry`, a module
// on `reload module` - instead of swapping the reference: anything already holding a reference into the OLD tables (an
// object whose metatable is a class from the old version) must run the NEW
// code on its next call, without the reload having to find and fix up every
// such reference itself.
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp. The two walks it runs live in
// rv_pccl_luajit_patchwalk_devtools.cpp. Dev-only: never built into a player binary (see
// CMakeLists.txt), so a player build never pays for this walk.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include <cassert>
#include <cstdint>

#include "lua.hpp"

#include "pdk/rv_err.h"
#include "rv_pconsole/cl/rv_pccl_luajit_detail.hpp"

namespace rv_3dmppc
{
namespace
{

// Carries the call across the protected-call boundary; no C++ return value
// or exception may cross it. `outcome` answers a CONTROLLED refusal (the
// node/depth bound); a real lua_pcall failure means only an allocation
// failed. `self` reaches state_ref_/loaded_ref_ to build LIVE (see patch_ctx).
struct patch_call_args {
    rv_pccl_luajit *self = nullptr;
    int old_ref = 0;
    int new_ref = 0;
    rv_pccl_reload_report *report = nullptr;
    int64_t outcome = RV_OK;
};

// Phase 3, function pass: every reachable Lua function's upvalues that hold
// a paired table are redirected to the OLD table - the whole reason a
// function copied wholesale into the old code still reaches live data
// through the tables that just moved under it.
void remap_upvalues(patch_ctx &ctx, int func_idx)
{
    lua_State *L = ctx.L;
    for (int i = 1;; ++i) {
        const char *name = lua_getupvalue(L, func_idx, i); // pushes the current value
        if (name == nullptr) {
            break;
        }
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        lua_rawget(L, ctx.map_idx); // consumes the upvalue as a key: map[upvalue] or nil
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        lua_setupvalue(L, func_idx, i); // pops the mapped table, installs it as upvalue i
    }
}

// remap(v): pushes map[v] when v is a table found in MAP, else pushes v
// itself. Leaves `v_idx` untouched.
void push_remap(patch_ctx &ctx, int v_idx)
{
    lua_State *L = ctx.L;
    if (lua_istable(L, v_idx)) {
        lua_pushvalue(L, v_idx);
        lua_rawget(L, ctx.map_idx);
        if (!lua_isnil(L, -1)) {
            return;
        }
        lua_pop(L, 1);
    }
    lua_pushvalue(L, v_idx);
}

// Phase 3, paired table: every field of `t` (new) is written into `o` (old)
// through remap(); a key `o` has that `t` does not is cleared - deleting the
// CURRENT key mid lua_next is the one mutation the reference manual allows
// during traversal, which is exactly what this needs. The metatable follows
// the same rule as a field.
void copy_paired(patch_ctx &ctx, int t_idx, int o_idx)
{
    lua_State *L = ctx.L;
    lua_pushvalue(L, t_idx);
    const int t_dup = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, t_dup) != 0) {
        // [key, value]
        const int key_idx = lua_gettop(L) - 1;
        const int val_idx = lua_gettop(L);
        lua_pushvalue(L, key_idx);
        push_remap(ctx, val_idx); // [key, value, key, remapped]
        lua_rawset(L, o_idx);     // o[key] = remapped; pops key, remapped
        lua_pop(L, 1);            // value; key stays for lua_next
    }
    lua_pop(L, 1); // t_dup

    lua_pushvalue(L, o_idx);
    const int o_dup = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, o_dup) != 0) {
        // [key, value]
        const int key_idx = lua_gettop(L) - 1;
        lua_pop(L, 1); // value
        lua_pushvalue(L, key_idx);
        lua_rawget(L, t_idx); // t[key]
        const bool present = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (!present) {
            lua_pushvalue(L, key_idx);
            lua_pushnil(L);
            lua_rawset(L, o_idx); // deletes the current key; lua_next may continue from it
        }
    }
    lua_pop(L, 1); // o_dup

    if (lua_getmetatable(L, t_idx)) { // pushes mt_n
        push_remap(ctx, lua_gettop(L));
        lua_remove(L, -2); // drop mt_n, keep remap(mt_n)
        lua_setmetatable(L, o_idx);
    } else if (lua_getmetatable(L, o_idx)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_setmetatable(L, o_idx);
    }
}

// Phase 3, unpaired table (new in this version): mutated in place, not
// copied anywhere - only its own field values and metatable are redirected
// where they point at a table that DID get paired.
void copy_unpaired(patch_ctx &ctx, int t_idx)
{
    lua_State *L = ctx.L;
    lua_pushvalue(L, t_idx);
    const int t_dup = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, t_dup) != 0) {
        // [key, value]
        const int key_idx = lua_gettop(L) - 1;
        const int val_idx = lua_gettop(L);
        if (lua_istable(L, val_idx)) {
            lua_pushvalue(L, val_idx);
            lua_rawget(L, ctx.map_idx);
            if (!lua_isnil(L, -1)) {
                lua_pushvalue(L, key_idx);
                lua_pushvalue(L, -2); // the mapped table
                lua_rawset(L, t_dup);
            }
            lua_pop(L, 1); // the map lookup
        }
        lua_pop(L, 1); // value; key stays for lua_next
    }
    lua_pop(L, 1); // t_dup

    if (lua_getmetatable(L, t_idx)) { // pushes mt
        lua_pushvalue(L, -1);
        lua_rawget(L, ctx.map_idx);
        if (!lua_isnil(L, -1)) {
            lua_setmetatable(L, t_idx); // pops the mapped mt
            lua_pop(L, 1);              // the original mt
        } else {
            lua_pop(L, 2); // nil, the original mt
        }
    }
}

} // namespace

// Runs entirely under the caller's lua_pcall: touserdata(1) is a
// patch_call_args*, and the only way this can fail from here on is an
// allocation failure the temporary map/seen tables or a lua_next hit, which
// surfaces as the pcall itself returning nonzero.
int rv_pccl_luajit::patch_trampoline_(lua_State *L)
{
    auto *args = static_cast<patch_call_args *>(lua_touserdata(L, 1));

    lua_newtable(L);
    const int map_idx = lua_gettop(L);
    lua_newtable(L);
    const int old_claimed_idx = lua_gettop(L);
    lua_newtable(L);
    const int pair_seen_idx = lua_gettop(L);
    lua_newtable(L);
    const int seen_idx = lua_gettop(L);
    lua_newtable(L);
    const int live_idx = lua_gettop(L);
    patch_ctx ctx{ L, map_idx, old_claimed_idx, pair_seen_idx, seen_idx, live_idx };

    lua_rawgeti(L, LUA_REGISTRYINDEX, args->old_ref);
    const int old_idx = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, args->new_ref);
    const int new_idx = lua_gettop(L);

    // LIVE, built once: the persistent state table and every loaded module -
    // the tables Pass 2 must treat as opaque leaves (see is_live/defect 2).
    lua_rawgeti(L, LUA_REGISTRYINDEX, args->self->state_ref_);
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, -1);
        lua_pushboolean(L, 1);
        lua_rawset(L, live_idx);
    }
    lua_pop(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, args->self->loaded_ref_);
    const int loaded_idx = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, loaded_idx) != 0) {
        if (lua_istable(L, -1)) {
            lua_pushvalue(L, -1);
            lua_pushboolean(L, 1);
            lua_rawset(L, live_idx);
        }
        lua_pop(L, 1); // value; key stays for lua_next
    }
    lua_pop(L, 1); // loaded table

    if (!patch_pair(ctx, old_idx, new_idx, 1) || ctx.refused) {
        args->outcome = RV_ERR_INVAL;
        args->report->phase = "patch";
        args->report->effects_possible = true;
        args->report->message = ctx.refuse_message != nullptr ? ctx.refuse_message
                                                                : "the candidate could not be patched in place";
        return 0;
    }
    if (!patch_reach(ctx, new_idx, 1) || ctx.refused) {
        args->outcome = RV_ERR_INVAL;
        args->report->phase = "patch";
        args->report->effects_possible = true;
        args->report->message = ctx.refuse_message != nullptr ? ctx.refuse_message
                                                                : "the candidate could not be patched in place";
        return 0;
    }

    // Function upvalues first: redirecting a closure at a paired table does
    // not depend on that table's own fields having been rewritten yet.
    lua_pushnil(L);
    while (lua_next(L, seen_idx) != 0) {
        if (lua_isfunction(L, -2) && !lua_iscfunction(L, -2)) {
            remap_upvalues(ctx, lua_gettop(L) - 1);
        }
        lua_pop(L, 1); // value; key stays for lua_next
    }

    lua_pushnil(L);
    while (lua_next(L, seen_idx) != 0) {
        if (lua_istable(L, -2)) {
            const int t_idx = lua_gettop(L) - 1;
            lua_pushvalue(L, t_idx);
            lua_rawget(L, map_idx);
            if (!lua_isnil(L, -1)) {
                copy_paired(ctx, t_idx, lua_gettop(L));
            } else {
                copy_unpaired(ctx, t_idx);
            }
            lua_pop(L, 1); // the map lookup
        }
        lua_pop(L, 1); // value; key stays for lua_next
    }

    args->outcome = RV_OK;
    return 0;
}

// Pass 1 pairs new tables with old ones by shape, Pass 2 walks the new graph
// to budget it and collect it into SEEN, Pass 3 remaps upvalues and copies
// fields - see patch_trampoline_ for the whole algorithm. `old_ref`'s tables
// are mutated to carry `new_ref`'s contents; `new_ref` itself is never
// touched by anything above (the caller unrefs it either way).
int64_t rv_pccl_luajit::patch_in_place_(int old_ref, int new_ref, rv_pccl_reload_report &report)
{
    [[maybe_unused]] const int top = lua_gettop(L_);

    patch_call_args args;
    args.self = this;
    args.old_ref = old_ref;
    args.new_ref = new_ref;
    args.report = &report;

    if (protected_call_(patch_trampoline_, &args) != 0) {
        lua_pop(L_, 1); // the error object; only an allocation failure can raise here
        report.phase = "nomem";
        report.effects_possible = true;
        report.message = "the heap ran out while replacing the code in place; the running code may be partly replaced";
        assert(lua_gettop(L_) == top);
        return RV_ERR_NOMEM;
    }
    assert(lua_gettop(L_) == top);
    return args.outcome;
}

} // namespace rv_3dmppc
