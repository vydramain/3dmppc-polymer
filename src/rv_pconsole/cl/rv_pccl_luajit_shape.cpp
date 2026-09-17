// The attachment entry and bookkeeping: manage the state_shape check and
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
	bool has_shape = false;
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
	args->has_shape = true;
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
	ctx.shape_seen.clear();
	ctx.state_seen.clear();
	walk_table(ctx, shape_idx, state_idx, "", 0);
	assert(!ctx.refused); // pass one already proved this tree valid

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

	lua_pushcfunction(L_, shape_trampoline_);
	lua_pushlightuserdata(L_, &args);
	const int rc = lua_pcall(L_, 1, 0, 0);
	if (rc != 0) {
		// An out-of-budget allocation while building a default table; undo
		// whatever this call had already written before the failure.
		const char *msg = lua_tostring(L_, -1);
		finish_state_shape_(inserted, false);
		report.phase = "state_shape";
		report.effects_possible = false; // only our own pending defaults existed; all rolled back
		report.message = std::string("could not install defaults: ") + (msg != nullptr ? msg : "(no message)");
		lua_pop(L_, 1);
		assert(lua_gettop(L_) == top);
		return RV_ERR_NOMEM;
	}
	assert(lua_gettop(L_) == top);

	if (!args.has_shape) {
		return RV_OK; // no state_shape declared: unchanged behaviour
	}
	if (args.refused) {
		finish_state_shape_(inserted, false); // nothing pending on a pass-one refusal, but be exact
		report.phase = "state_shape";
		report.effects_possible = false;
		// A refusal at the top level has no field path, and a bare ": reason"
		// reads like a truncated message.
		report.message = args.refuse_path.empty()
			? args.refuse_message
			: args.refuse_path + ": " + args.refuse_message;
		return RV_ERR_INVAL;
	}
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
