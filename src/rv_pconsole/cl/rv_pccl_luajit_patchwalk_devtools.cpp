// The in-place patch's two walks: pairing the candidate's tables with the
// running ones, and counting what the candidate reaches. Dev slot only (see
// CMakeLists.txt); patch_trampoline_ in rv_pccl_luajit_patch_devtools.cpp runs them.
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include "lua.hpp"

#include "pdk/rv_err.h"
#include "rv_pconsole/cl/rv_pccl_luajit_detail.hpp"

namespace rv_3dmppc
{
namespace
{

// First sighting of `n_idx` (absolute index) returns true and charges the
// node budget; a table or function already seen returns false so the caller
// stops there - a cycle or a diamond is visited once, not walked forever.
// Pass 2 only: Pass 1 has its own MAP/PAIR_SEEN bookkeeping instead.
bool first_visit(patch_ctx &ctx, int n_idx)
{
    lua_State *L = ctx.L;
    lua_pushvalue(L, n_idx);
    lua_rawget(L, ctx.seen_idx);
    const bool seen_before = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (seen_before) {
        return false;
    }
    lua_pushvalue(L, n_idx);
    lua_pushboolean(L, 1);
    lua_rawset(L, ctx.seen_idx);
    if (++ctx.nodes > RV_PCCL_PATCH_NODE_MAX) {
        ctx.refused = true;
        ctx.refuse_message = "the candidate reaches more than RV_PCCL_PATCH_NODE_MAX tables/functions";
    }
    return true;
}

// Prepare and pair new metatable with old if available, otherwise with nil
int pair_metatable_(patch_ctx &ctx, bool paired, int o_idx, int depth)
{
    lua_State *L = ctx.L;
    const int mt_n_idx = lua_gettop(L);
    const bool has_mt_o = paired && lua_getmetatable(L, o_idx); // pushes mt_o on success
    if (!has_mt_o) {
        lua_pushnil(L);
    }
    const int err = patch_pair(ctx, lua_gettop(L), mt_n_idx, depth + 1);
    lua_pop(L, 1); // mt_o or nil
    if (err != RV_OK) {
        lua_pop(L, 1); // mt_n
        return err;
    }
    lua_pop(L, 1); // mt_n
    return RV_OK;
}

// Prepare and pair new field value with old if available, otherwise with nil
int pair_field_(patch_ctx &ctx, bool paired, int o_idx, int key_idx, int val_idx, int depth)
{
    lua_State *L = ctx.L;
    if (paired) {
        lua_pushvalue(L, key_idx);
        lua_rawget(L, o_idx); // oc = o[key], or nil if absent
    } else {
        lua_pushnil(L);
    }
    const int err = patch_pair(ctx, lua_gettop(L), val_idx, depth + 1);
    lua_pop(L, 1); // oc or nil
    return err;
}

// Is `idx` a table Pass 2 must treat as an opaque leaf: an old table already
// claimed by a pair, or one of the live tables built in patch_trampoline_ -
// neither is safe to count against the node budget or walk into.
bool is_live(patch_ctx &ctx, int idx)
{
    lua_State *L = ctx.L;
    if (!lua_istable(L, idx)) {
        return false;
    }
    lua_pushvalue(L, idx);
    lua_rawget(L, ctx.old_claimed_idx);
    const bool claimed = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (claimed) {
        return true;
    }
    lua_pushvalue(L, idx);
    lua_rawget(L, ctx.live_idx);
    const bool live = !lua_isnil(L, -1);
    lua_pop(L, 1);
    return live;
}

// Pass 2, function pass: every reachable Lua function's upvalues, counted
// and recursed into for the node/depth budget, and remapped in
// remap_upvalues below - a function's bytecode is never patched, only the
// tables it closes over may need to move.
// Returns RV_OK on success, RV_ERR_INVAL on budget exhaustion or refusal.
int reach_count_function(patch_ctx &ctx, int n_idx, int depth)
{
    lua_State *L = ctx.L;
    if (depth > RV_PCCL_PATCH_DEPTH_MAX) {
        ctx.refused = true;
        ctx.refuse_message = "the candidate nests deeper than RV_PCCL_PATCH_DEPTH_MAX";
        return RV_ERR_INVAL;
    }
    if (!first_visit(ctx, n_idx)) {
        return RV_OK;
    }
    if (ctx.refused) {
        return RV_ERR_INVAL;
    }
    for (int i = 1;; ++i) {
        const char *name = lua_getupvalue(L, n_idx, i); // pushes the upvalue's current value
        if (name == nullptr) {
            break;
        }
        const int up_idx = lua_gettop(L);
        int err = RV_OK;
        switch (lua_type(L, up_idx)) {
        case LUA_TTABLE:
            err = patch_reach(ctx, up_idx, depth + 1);
            break;
        case LUA_TFUNCTION:
            if (!lua_iscfunction(L, up_idx)) {
                err = reach_count_function(ctx, up_idx, depth + 1);
            }
            break;
        }
        lua_pop(L, 1); // the upvalue value
        if (err != RV_OK) {
            return err;
        }
    }
    return RV_OK;
}

} // namespace

// Pass 1, fields and metatables only - no upvalues, no functions: pairs
// `n_idx` (a new table) with `o_idx` (its old counterpart, or nil) when
// `o_idx` is an unclaimed table, then recurses into the metatable and every
// table field of `n_idx`. A new table already in MAP (paired) or PAIR_SEEN
// (visited, left unpaired) stops here, so a cycle ends either way.
// Returns RV_OK on success, RV_ERR_INVAL on budget exhaustion or refusal.
int patch_pair(patch_ctx &ctx, int o_idx, int n_idx, int depth)
{
    lua_State *L = ctx.L;
    if (!lua_istable(L, n_idx)) {
        return RV_OK;
    }
    if (depth > RV_PCCL_PATCH_DEPTH_MAX) {
        ctx.refused = true;
        ctx.refuse_message = "the candidate nests deeper than RV_PCCL_PATCH_DEPTH_MAX";
        return RV_ERR_INVAL;
    }

    lua_pushvalue(L, n_idx);
    lua_rawget(L, ctx.map_idx);
    const bool already_mapped = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (already_mapped) {
        return RV_OK;
    }
    lua_pushvalue(L, n_idx);
    lua_rawget(L, ctx.pair_seen_idx);
    const bool already_visited = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (already_visited) {
        return RV_OK;
    }

    bool paired = false;
    if (lua_istable(L, o_idx)) {
        lua_pushvalue(L, o_idx);
        lua_rawget(L, ctx.old_claimed_idx);
        const bool claimed = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (!claimed) {
            lua_pushvalue(L, n_idx);
            lua_pushvalue(L, o_idx);
            lua_rawset(L, ctx.map_idx); // map[n] = o
            lua_pushvalue(L, o_idx);
            lua_pushboolean(L, 1);
            lua_rawset(L, ctx.old_claimed_idx); // old_claimed[o] = true
            paired = true;
        }
    }
    if (!paired) {
        lua_pushvalue(L, n_idx);
        lua_pushboolean(L, 1);
        lua_rawset(L, ctx.pair_seen_idx); // stop a later revisit of this unpaired table
    }

    if (lua_getmetatable(L, n_idx)) { // pushes mt_n (always a table)
        const int err = pair_metatable_(ctx, paired, o_idx, depth);
        if (err != RV_OK) {
            return err;
        }
    }

    lua_pushnil(L);
    while (lua_next(L, n_idx) != 0) {
        // [key, value]
        const int key_idx = lua_gettop(L) - 1;
        const int val_idx = lua_gettop(L);
        if (lua_istable(L, val_idx)) {
            const int err = pair_field_(ctx, paired, o_idx, key_idx, val_idx, depth);
            if (err != RV_OK) {
                lua_pop(L, 2); // value, key
                return err;
            }
        }
        lua_pop(L, 1); // value; key stays for lua_next
    }
    return RV_OK;
}

// Pass 2, reachability only - no pairing code: counts and recurses into
// `n_idx`'s metatable, fields and (through reach_count_function) function
// upvalues, budgeted by SEEN/nodes/depth. A LIVE table (the persistent state,
// a loaded module, or an old table already claimed by Pass 1) is neither
// counted nor recursed into - it is used exactly as it already is.
// Returns RV_OK on success, RV_ERR_INVAL on budget exhaustion or refusal.
int patch_reach(patch_ctx &ctx, int n_idx, int depth)
{
    lua_State *L = ctx.L;
    if (lua_isfunction(L, n_idx) && !lua_iscfunction(L, n_idx)) {
        return reach_count_function(ctx, n_idx, depth);
    }
    if (!lua_istable(L, n_idx)) {
        return RV_OK; // a C function, or a scalar reached as some value - nothing to patch
    }
    if (is_live(ctx, n_idx)) {
        return RV_OK;
    }
    if (depth > RV_PCCL_PATCH_DEPTH_MAX) {
        ctx.refused = true;
        ctx.refuse_message = "the candidate nests deeper than RV_PCCL_PATCH_DEPTH_MAX";
        return RV_ERR_INVAL;
    }
    if (!first_visit(ctx, n_idx)) {
        return RV_OK;
    }
    if (ctx.refused) {
        return RV_ERR_INVAL;
    }

    if (lua_getmetatable(L, n_idx)) { // pushes mt (always a table)
        const int mt_idx = lua_gettop(L);
        const int err = patch_reach(ctx, mt_idx, depth + 1);
        lua_pop(L, 1); // mt
        if (err != RV_OK) {
            return err;
        }
    }
    if (ctx.refused) {
        return RV_ERR_INVAL;
    }

    lua_pushnil(L);
    while (lua_next(L, n_idx) != 0) {
        // [key, value]
        const int val_idx = lua_gettop(L);
        int err = RV_OK;
        switch (lua_type(L, val_idx)) {
        case LUA_TTABLE:
            err = patch_reach(ctx, val_idx, depth + 1);
            break;
        case LUA_TFUNCTION:
            if (!lua_iscfunction(L, val_idx)) {
                err = reach_count_function(ctx, val_idx, depth + 1);
            }
            break;
        }
        lua_pop(L, 1); // value; key stays for lua_next
        if (err != RV_OK) {
            lua_pop(L, 1); // key
            return err;
        }
    }
    return RV_OK;
}

} // namespace rv_3dmppc
