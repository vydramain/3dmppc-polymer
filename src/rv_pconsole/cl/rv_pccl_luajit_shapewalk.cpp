// The structural walk itself: recursively read the type of every key the live
// state table holds, charging the same node/depth budget the walk has always
// used, and - on a reload, where `ctx.old_shape` is not null - refusing a key
// whose type no longer matches what the console remembered for it. This file
// no longer compares two trees (a declared shape and the state); there is
// only one tree now, the state itself, and `ctx.old_shape`/`ctx.fresh` are a
// map from BEFORE this walk and the map it is building, not a second table on
// the lua stack.
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include <cstddef>
#include <string>

#include "lua.hpp"

#include "rv_pconsole/cl/rv_pccl_luajit_detail.hpp"

namespace rv_3dmppc
{
namespace
{

// The only four types a state value may hold. A function or a coroutine
// would keep the OLD chunk's bytecode alive past a reload - the exact thing
// "code != state" promises cannot happen (see example-lua.lua's own comment
// on `state`); userdata and thread have no business here either.
bool legal_state_type(int t)
{
    return t == LUA_TBOOLEAN || t == LUA_TNUMBER || t == LUA_TSTRING || t == LUA_TTABLE;
}

// A lua_next key to its text form, WITHOUT lua_tostring'ing the original:
// converting a number key in place is the one mutation lua_next's contract
// forbids during iteration, so a number key is converted on a throwaway
// duplicate. Only string and number keys have a text form at all - anything
// else is refused rather than guessed at. Stack-neutral on both paths.
bool key_to_string(lua_State *L, int key_idx, shape_capture_ctx &ctx, const std::string &parent_path,
    std::string &out)
{
    const int t = lua_type(L, key_idx);
    if (t != LUA_TSTRING && t != LUA_TNUMBER) {
        ctx.refused = true;
        ctx.refuse_path = parent_path;
        ctx.refuse_message = std::string("state key must be a string or number, found ") + lua_typename(L, t);
        return false;
    }
    if (t == LUA_TSTRING) {
        std::size_t len = 0;
        const char *s = lua_tolstring(L, key_idx, &len);
        out.assign(s, len);
        return true;
    }
    lua_pushvalue(L, key_idx);
    std::size_t len = 0;
    const char *s = lua_tolstring(L, -1, &len);
    out.assign(s, len);
    lua_pop(L, 1);
    return true;
}

} // namespace

// The one entry the sibling translation unit calls (rv_pccl_luajit_shape.cpp),
// so it alone has external linkage; everything above is this file's own.
//
// `table_idx` is a table on the lua stack (the live state table, or one of
// its own sub-tables on a recursive call) - there is no second, shape-side
// table to walk in parallel any more.
bool capture_walk(shape_capture_ctx &ctx, int table_idx, const std::string &path, int depth)
{
    lua_State *L = ctx.L;
    if (depth > kShapeMaxDepth) {
        ctx.refused = true;
        ctx.refuse_path = path;
        ctx.refuse_message = "state nested too deep";
        return false;
    }
    if (++ctx.nodes > kShapeMaxNodes) {
        ctx.refused = true;
        ctx.refuse_path = path;
        ctx.refuse_message = "state has too many fields";
        return false;
    }

    lua_pushnil(L);
    while (lua_next(L, table_idx) != 0) {
        // [key, value]
        std::string kstr;
        if (!key_to_string(L, -2, ctx, path, kstr)) {
            lua_pop(L, 2);
            return false;
        }
        const std::string child_path = path.empty() ? kstr : path + "." + kstr;
        if (++ctx.nodes > kShapeMaxNodes) {
            ctx.refused = true;
            ctx.refuse_path = child_path;
            ctx.refuse_message = "state has too many fields";
            lua_pop(L, 2);
            return false;
        }

        const int value_idx = lua_gettop(L);
        const int t = lua_type(L, value_idx);
        if (!legal_state_type(t)) {
            ctx.refused = true;
            ctx.refuse_path = child_path;
            ctx.refuse_message = std::string("state holds a value it must not carry: a ") + lua_typename(L, t);
            lua_pop(L, 2);
            return false;
        }

        // The comparison against what the console remembers - the only thing
        // that can refuse a RELOAD candidate, since a brand new key never does
        // (see capture_state_shape_'s own comment for why that is a judgement
        // call, not an oversight).
        if (!ctx.initial) {
            const auto it = ctx.old_shape->find(child_path);
            if (it != ctx.old_shape->end() && it->second != t) {
                ctx.refused = true;
                ctx.refuse_path = child_path;
                ctx.refuse_message =
                    std::string("expected ") + lua_typename(L, it->second) + ", found " + lua_typename(L, t);
                lua_pop(L, 2);
                return false;
            }
        }

        (*ctx.fresh)[child_path] = t;
        if (t == LUA_TTABLE) {
            const bool ok = capture_walk(ctx, value_idx, child_path, depth + 1);
            lua_pop(L, 1); // value; key stays for lua_next
            if (!ok) {
                return false;
            }
            continue;
        }
        lua_pop(L, 1); // value; key stays for lua_next
    }
    return true;
}

} // namespace rv_3dmppc
