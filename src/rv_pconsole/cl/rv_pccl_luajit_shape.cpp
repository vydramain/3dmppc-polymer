// The state shape the console remembers, and the bookkeeping around it: the
// trampoline that runs capture_walk (_shapewalk.cpp) under lua_pcall, and the
// snapshot/restore pair that makes a reload's refusal cost nothing - the live
// state table reads exactly as it did before the refused candidate's body
// ever ran.
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include <cassert>
#include <map>
#include <string>
#include <utility>

#include "lua.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cl/rv_pccl_luajit_detail.hpp"

namespace rv_3dmppc
{
namespace
{

// Argument bundle for shape_capture_trampoline_, passed as light userdata:
// the protected call boundary means neither an exception nor a C++ return
// value crosses it, so the answer travels back through this struct instead.
struct capture_call_args {
    rv_pccl_luajit *self = nullptr;
    bool initial = false;
    std::map<std::string, int> fresh;
    bool refused = false;
    std::string refuse_path;
    std::string refuse_message;
};

} // namespace

int rv_pccl_luajit::shape_capture_trampoline_(lua_State *L)
{
    auto *args = static_cast<capture_call_args *>(lua_touserdata(L, 1));
    rv_pccl_luajit *self = args->self;

    lua_rawgeti(L, LUA_REGISTRYINDEX, self->state_ref_);
    const int state_idx = lua_gettop(L);

    shape_capture_ctx ctx;
    ctx.L = L;
    ctx.initial = args->initial;
    ctx.old_shape = args->initial ? nullptr : &self->state_shape_;
    ctx.fresh = &args->fresh;

    const bool ok = capture_walk(ctx, state_idx, "", 0);
    if (!ok) {
        args->refused = true;
        args->refuse_path = ctx.refuse_path;
        args->refuse_message = ctx.refuse_message;
    }
    return 0;
}

// `initial=true` is the boot capture: state has just been populated by the
// entry chunk's own disc_initialize (see script_call, which is the only
// caller that passes true, and only once - the moment that hook returns is
// the moment the script has finished setting up `state`, which is why the
// console reads the shape FROM state instead of asking the chunk to declare
// one). `initial=false` is a reload candidate: state is compared against what
// was remembered from the last accepted capture, and the walk itself is what
// decides the three cases a PR review asked for by name - a key whose type
// changed refuses (capture_walk finds it), a brand new key is simply folded
// into `fresh` with no comparison at all, and a key the new code stopped
// writing is not visited and therefore silently absent from `fresh`. Either
// way, once the walk finishes clean, `fresh` REPLACES state_shape_ wholesale -
// that single assignment is what makes "dropped" and "joined" happen without
// two more code paths to keep in sync with the walk.
int64_t rv_pccl_luajit::capture_state_shape_(bool initial, rv_pccl_reload_report &report)
{
    [[maybe_unused]] const int top = lua_gettop(L_);

    capture_call_args args;
    args.self = this;
    args.initial = initial;

    const int rc = protected_call_(shape_capture_trampoline_, &args);
    if (rc != 0) {
        const char *msg = lua_tostring(L_, -1);
        report.phase = "state_shape";
        report.effects_possible = true; // whatever wrote the offending value already ran
        report.message = std::string("could not read state: ") + (msg != nullptr ? msg : "(no message)");
        lua_pop(L_, 1);
        assert(lua_gettop(L_) == top);
        return RV_ERR_NOMEM;
    }
    assert(lua_gettop(L_) == top);

    if (args.refused) {
        report.phase = "state_shape";
        report.effects_possible = true;
        report.message =
            args.refuse_path.empty() ? args.refuse_message : args.refuse_path + ": " + args.refuse_message;
        return RV_ERR_INVAL;
    }

    state_shape_ = std::move(args.fresh);
    report.phase = "ok";
    report.effects_possible = false;
    report.message.clear();
    return RV_OK;
}

namespace
{

// Recursively makes `dst` (assumed EMPTY on entry - a fresh table, or one
// table_clear_ below just emptied) hold a copy of every key and value `src`
// has. Used in both directions: state -> a snapshot before a reload
// candidate's body runs, and snapshot -> state again should that candidate be
// refused. Bounded by the same depth/node budget capture_walk uses, and for
// the same reason - this walks data a script put in `state`, which the shape
// walk itself only ever certified up to that same budget in the first place.
bool mirror_table_(lua_State *L, int dst_idx, int src_idx, int &nodes, int depth)
{
    if (depth > kShapeMaxDepth || ++nodes > kShapeMaxNodes) {
        return false;
    }
    lua_pushnil(L);
    while (lua_next(L, src_idx) != 0) {
        // [key, value]
        if (++nodes > kShapeMaxNodes) {
            lua_pop(L, 2);
            return false;
        }
        const int value_idx = lua_gettop(L);
        if (lua_type(L, value_idx) == LUA_TTABLE) {
            lua_newtable(L); // [key, value, child]
            const int child_idx = lua_gettop(L);
            if (!mirror_table_(L, child_idx, value_idx, nodes, depth + 1)) {
                lua_pop(L, 3); // child, value, key
                return false;
            }
            lua_pushvalue(L, -3); // [key, value, child, key]
            lua_insert(L, -2);    // [key, value, key, child]
            lua_rawset(L, dst_idx); // dst[key] = child; [key, value]
            lua_pop(L, 1);           // drop value; key stays for lua_next
            continue;
        }
        // A scalar - the only other shape a legal state value can have.
        lua_pushvalue(L, -2);        // [key, value, key]
        lua_pushvalue(L, value_idx); // [key, value, key, value]
        lua_rawset(L, dst_idx);      // dst[key] = value; [key, value]
        lua_pop(L, 1);               // drop value; key stays for lua_next
    }
    return true;
}

// Empties a table IN PLACE, never replacing it: every other reference to this
// exact table object - in particular entry_env_ref_'s `state` field, and
// every closure the entry chunk has ever read `state` through - keeps working
// and simply sees zero keys until mirror_table_ repopulates it right after.
// Keys are collected into a scratch table first, because deleting one
// lua_next has not reached yet is not part of its contract.
void table_clear_(lua_State *L, int idx)
{
    lua_newtable(L);
    const int keys_idx = lua_gettop(L);
    lua_Integer n = 0;
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        lua_pop(L, 1);                  // value
        lua_pushvalue(L, -1);           // dup the key
        lua_rawseti(L, keys_idx, ++n);  // keys[n] = key; key itself stays for lua_next
    }
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, keys_idx, i);
        lua_pushnil(L);
        lua_rawset(L, idx);
    }
    lua_pop(L, 1); // keys
}

struct snapshot_args {
    rv_pccl_luajit *self = nullptr;
    int ref_out = -1;
    bool ok = false;
};

struct restore_args {
    rv_pccl_luajit *self = nullptr;
    int snapshot_ref = 0;
    bool ok = false;
};

} // namespace

int rv_pccl_luajit::state_snapshot_trampoline_(lua_State *L)
{
    auto *args = static_cast<snapshot_args *>(lua_touserdata(L, 1));
    rv_pccl_luajit *self = args->self;

    lua_newtable(L);
    const int dst_idx = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->state_ref_);
    const int src_idx = lua_gettop(L);

    int nodes = 0;
    args->ok = mirror_table_(L, dst_idx, src_idx, nodes, 0);
    lua_pop(L, 1); // src
    if (args->ok) {
        args->ref_out = luaL_ref(L, LUA_REGISTRYINDEX); // pops dst
    } else {
        lua_pop(L, 1); // dst
    }
    return 0;
}

// Taken BEFORE a reload candidate is raised: raise_ runs the candidate's
// chunk body as part of compiling it, and that body already has `state`
// reachable (the same entry_env_ref_ wiring every hook uses) - so a candidate
// that writes to state at its top level, rather than inside a hook, reaches
// the SAME table the currently-running old code depends on. This is what lets
// a later refusal give that table back exactly as it was, instead of only
// reporting that it was disturbed.
int64_t rv_pccl_luajit::snapshot_state_(int &ref_out, rv_pccl_reload_report &report)
{
    [[maybe_unused]] const int top = lua_gettop(L_);

    snapshot_args args;
    args.self = this;

    const int rc = protected_call_(state_snapshot_trampoline_, &args);
    if (rc != 0) {
        lua_pop(L_, 1);
        assert(lua_gettop(L_) == top);
        report.phase = "state_shape";
        report.effects_possible = false; // nothing of the candidate has run yet
        report.message = "the running state could not be copied to protect it for this reload; try gc";
        return RV_ERR_NOMEM;
    }
    assert(lua_gettop(L_) == top);
    if (!args.ok) {
        report.phase = "state_shape";
        report.effects_possible = false;
        report.message = "the running state is too large or too deeply nested to protect for a reload";
        return RV_ERR_INVAL;
    }
    ref_out = args.ref_out;
    return RV_OK;
}

int rv_pccl_luajit::state_restore_trampoline_(lua_State *L)
{
    auto *args = static_cast<restore_args *>(lua_touserdata(L, 1));
    rv_pccl_luajit *self = args->self;

    lua_rawgeti(L, LUA_REGISTRYINDEX, self->state_ref_);
    const int dst_idx = lua_gettop(L);
    table_clear_(L, dst_idx);
    lua_rawgeti(L, LUA_REGISTRYINDEX, args->snapshot_ref);
    const int src_idx = lua_gettop(L);

    int nodes = 0;
    args->ok = mirror_table_(L, dst_idx, src_idx, nodes, 0);
    lua_pop(L, 2); // src, dst
    return 0;
}

// Undoes exactly what snapshot_state_ protected: whatever the refused
// candidate's body wrote into `state` is gone, and the live table reads
// again as it did the moment before raise_ ran that body - which is what
// lets the old code keep going with its data intact instead of merely
// keeping its bytecode. The snapshot ref is released either way; a restore
// that cannot itself finish (an allocation failure partway through
// repopulating an already-emptied table) is the one shape of loss this
// cannot promise against, the same limit every other console-side walk over
// the script heap already lives with.
void rv_pccl_luajit::restore_state_(int snapshot_ref)
{
    [[maybe_unused]] const int top = lua_gettop(L_);
    restore_args args;
    args.self = this;
    args.snapshot_ref = snapshot_ref;
    if (protected_call_(state_restore_trampoline_, &args) != 0) {
        lua_pop(L_, 1);
        RV_LOG_ERR("pccl", "state rollback ran out of script heap partway through; state may be incomplete");
    } else if (!args.ok) {
        RV_LOG_ERR("pccl", "state rollback stopped at its own depth/node bound; state may be incomplete");
    }
    assert(lua_gettop(L_) == top);
    luaL_unref(L_, LUA_REGISTRYINDEX, snapshot_ref);
}

} // namespace rv_3dmppc
