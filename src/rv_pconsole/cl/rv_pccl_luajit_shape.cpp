// The state_shape check and the bookkeeping around it: the walk itself, and
// the insertions it makes to the live state table.
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include <cassert>
#include <string>
#include <vector>

#include "lua.hpp"

#include "pdk/rv_err.h"
#include "rv_pconsole/cl/rv_pccl_luajit_detail.hpp"

namespace rv_3dmppc
{
namespace
{

// Argument bundle for shape_trampoline_, passed as light userdata: the
// protected call boundary means neither an exception nor a C++ return value
// crosses it, so the answer travels back through this struct instead.
struct shape_call_args {
    rv_pccl_luajit *self = nullptr;
    int ref = 0;
    std::vector<rv_pccl_luajit::state_shape_insert> *inserted = nullptr;
    bool refused = false;
    std::string refuse_path;
    std::string refuse_message;
};

} // namespace

int rv_pccl_luajit::shape_trampoline_(lua_State *L)
{
    auto *args = static_cast<shape_call_args *>(lua_touserdata(L, 1));
    rv_pccl_luajit *self = args->self;

    lua_rawgeti(L, LUA_REGISTRYINDEX, args->ref); // [module]
    lua_pushstring(L, "state_shape");
    lua_rawget(L, -2); // [module, shape?]
    const int shape_type = lua_type(L, -1);
    if (shape_type == LUA_TNIL) {
        lua_pop(L, 2);
        return 0; // no declaration: behaviour is exactly what it was before this check existed
    }
    if (shape_type != LUA_TTABLE) {
        lua_pop(L, 2);
        args->refused = true;
        args->refuse_path = "state_shape";
        args->refuse_message = "state_shape must be a table";
        return 0;
    }
    const int shape_idx = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->state_ref_); // [module, shape, state]
    const int state_idx = lua_gettop(L);

    shape_walk_ctx ctx;
    ctx.L = L;
    ctx.inserted = args->inserted;

    // Pass one: validate the whole tree, mutate nothing.
    ctx.apply = false;
    const bool valid = walk_table(ctx, shape_idx, state_idx, "", 0);
    if (!valid) {
        args->refused = true;
        args->refuse_path = ctx.refuse_path;
        args->refuse_message = ctx.refuse_message;
        lua_pop(L, 3);
        return 0;
    }

    // Pass two: nothing changed since pass one, so the same walk now applies
    // the insertions it would have planned.
    ctx.apply = true;
    ctx.nodes = 0;
    ctx.shape_path.clear();
    ctx.state_seen.clear();
    // Checked, not asserted. An assert is gone in a release build, and this one
    // was: pass two used to exhaust the node budget on a tree pass one had
    // accepted, its refusal went unread, and the reload answered ok with the
    // state HALF grown - 4438 of 5000 fields inserted, and the candidate's own
    // hooks then running against it. Pass two failing is a console bug either
    // way, but a refusal the caller can roll back beats a success that is not
    // one.
    if (!walk_table(ctx, shape_idx, state_idx, "", 0)) {
        args->refused = true;
        args->refuse_path = ctx.refuse_path;
        args->refuse_message = ctx.refuse_message;
    }

    lua_pop(L, 3);
    return 0;
}

int64_t rv_pccl_luajit::check_state_shape_(int ref, std::vector<state_shape_insert> &inserted,
    rv_pccl_reload_report &report)
{
    [[maybe_unused]] const int top = lua_gettop(L_);

    shape_call_args args;
    args.self = this;
    args.ref = ref;
    args.inserted = &inserted;

    const int rc = protected_call_(shape_trampoline_, &args);
    if (rc != 0) {
        // An out-of-budget allocation while building a default table; undo
        // whatever this call had already written before the failure.
        const char *msg = lua_tostring(L_, -1);
        finish_state_shape_(inserted, false);
        report.phase = "state_shape";
        // The console's own pending defaults are all rolled back - but this
        // check runs AFTER raise_ has executed the candidate's body, and that
        // body may have written anything it could reach. effects_possible is a
        // conservative statement about the whole attempt, not about this step.
        report.effects_possible = true;
        report.message = std::string("could not install defaults: ") + (msg != nullptr ? msg : "(no message)");
        lua_pop(L_, 1);
        assert(lua_gettop(L_) == top);
        return RV_ERR_NOMEM;
    }
    assert(lua_gettop(L_) == top);

    // The refusal is checked BEFORE the declaration, because a state_shape
    // that is not a table is a refusal WITHOUT a shape: asking "was one
    // declared" first answered no and let `state_shape = 42` install itself
    // unchecked - the one kind of candidate this whole walk exists to stop.
    if (args.refused) {
        // A pass-one refusal has nothing pending; a pass-two refusal does, and
        // this is what takes those insertions back out.
        finish_state_shape_(inserted, false);
        report.phase = "state_shape";
        report.effects_possible = true; // the body ran before the walk did; see above
        // A refusal at the top level has no field path, and a bare ": reason"
        // reads like a truncated message.
        report.message = args.refuse_path.empty() ? args.refuse_message : args.refuse_path + ": " + args.refuse_message;
        return RV_ERR_INVAL;
    }
    // Reached both when the walk accepted the tree and when the chunk declared
    // no state_shape at all; the second is the documented unchanged behaviour,
    // and neither has anything left to report.
    return RV_OK;
}

void rv_pccl_luajit::finish_state_shape_(std::vector<state_shape_insert> &inserted, bool keep)
{
    for (const auto &ins : inserted) {
        if (!keep) {
            lua_rawgeti(L_, LUA_REGISTRYINDEX, ins.parent_ref);
            lua_pushstring(L_, ins.key.c_str());
            lua_pushnil(L_);
            lua_rawset(L_, -3);
            lua_pop(L_, 1);
        }
        luaL_unref(L_, LUA_REGISTRYINDEX, ins.parent_ref);
    }
    inserted.clear();
}

} // namespace rv_3dmppc
