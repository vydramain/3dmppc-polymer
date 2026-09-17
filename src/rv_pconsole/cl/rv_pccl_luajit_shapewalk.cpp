// The structural walk itself: compare a state table against a shape
// declaration, validate on pass one, apply insertions on pass two.
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

#include "lua.hpp"

#include "pdk/rv_err.h"
#include "rv_pconsole/cl/rv_pccl_luajit_detail.hpp"

namespace rv_3dmppc
{
namespace
{

// Defined below handle_missing, which calls it, so it needs this.
bool default_from_shape(shape_walk_ctx &ctx, int shape_idx, const std::string &path, int depth, bool build);

// A lua_next key to text, WITHOUT lua_tostring'ing the original: converting a
// number key in place is the one mutation lua_next's contract forbids during
// iteration, so a number key is converted on a throwaway duplicate.
std::string key_to_string(lua_State *L, int key_idx)
{
	if (lua_type(L, key_idx) == LUA_TSTRING) {
		std::size_t len = 0;
		const char *s = lua_tolstring(L, key_idx, &len);
		return std::string(s, len);
	}
	lua_pushvalue(L, key_idx);
	std::size_t len = 0;
	const char *s = lua_tolstring(L, -1, &len);
	std::string out(s, len);
	lua_pop(L, 1);
	return out;
}

// A declared key absent from state: build its default from the shape alone
// (build=false just validates that subtree is well formed) and, when
// applying, write it into `state_parent_idx[key]` and pin that parent so the
// insertion can be undone later. Stack-neutral on every path.
bool handle_missing(shape_walk_ctx &ctx, int shape_val_idx, int state_parent_idx, const std::string &key,
	const std::string &child_path, int depth)
{
	if (!ctx.apply) {
		return default_from_shape(ctx, shape_val_idx, child_path, depth, false);
	}
	if (!default_from_shape(ctx, shape_val_idx, child_path, depth, true)) {
		return false;
	}
	lua_State *L = ctx.L;
	const int default_idx = lua_gettop(L);
	lua_pushvalue(L, state_parent_idx);
	const int parent_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushstring(L, key.c_str());
	lua_pushvalue(L, default_idx);
	lua_rawset(L, state_parent_idx);
	lua_pop(L, 1); // the default itself; a copy of it is now in the table
	ctx.inserted->push_back(rv_pccl_luajit::state_shape_insert{ parent_ref, key });
	return true;
}

// Reads one shape node in isolation and, when build, pushes exactly the
// default value it describes (a scalar copy, an empty table for an open
// collection, or a freshly built table of defaults). Recurses through the
// SHAPE only - there is no state on this path, the field is absent.
bool default_from_shape(shape_walk_ctx &ctx, int shape_idx, const std::string &path, int depth, bool build)
{
	lua_State *L = ctx.L;
	if (depth > kShapeMaxDepth) {
		ctx.refused = true;
		ctx.refuse_path = path;
		ctx.refuse_message = "state_shape nested too deep";
		return false;
	}
	if (++ctx.nodes > kShapeMaxNodes) {
		ctx.refused = true;
		ctx.refuse_path = path;
		ctx.refuse_message = "state_shape has too many fields";
		return false;
	}

	const int t = lua_type(L, shape_idx);
	if (t == LUA_TBOOLEAN || t == LUA_TNUMBER || t == LUA_TSTRING) {
		if (build) {
			lua_pushvalue(L, shape_idx);
		}
		return true;
	}
	if (t != LUA_TTABLE) {
		ctx.refused = true;
		ctx.refuse_path = path;
		ctx.refuse_message = "state_shape declares an unsupported value";
		return false;
	}

	const void *sp = lua_topointer(L, shape_idx);
	if (std::find(ctx.shape_seen.begin(), ctx.shape_seen.end(), sp) != ctx.shape_seen.end()) {
		ctx.refused = true;
		ctx.refuse_path = path;
		ctx.refuse_message = "state_shape reuses the same table on two paths";
		return false;
	}
	ctx.shape_seen.push_back(sp);

	std::vector<std::string> keys;
	lua_pushnil(L);
	while (lua_next(L, shape_idx) != 0) {
		if (lua_type(L, -2) != LUA_TSTRING) {
			lua_pop(L, 2);
			ctx.refused = true;
			ctx.refuse_path = path;
			ctx.refuse_message = "state_shape key must be a string";
			return false;
		}
		keys.emplace_back(lua_tostring(L, -2));
		lua_pop(L, 1);
	}

	// An open collection with nothing in it yet defaults to empty: the "*"
	// entry is a per-key SHAPE, never itself a literal value to insert.
	if (keys.size() == 1 && keys[0] == "*") {
		if (build) {
			lua_newtable(L);
		}
		return true;
	}

	if (!build) {
		for (const auto &key : keys) {
			const std::string child_path = path.empty() ? key : path + "." + key;
			lua_pushstring(L, key.c_str());
			lua_rawget(L, shape_idx);
			const bool ok = default_from_shape(ctx, lua_gettop(L), child_path, depth + 1, false);
			lua_pop(L, 1);
			if (!ok) {
				return false;
			}
		}
		return true;
	}

	lua_newtable(L);
	const int new_idx = lua_gettop(L);
	for (const auto &key : keys) {
		const std::string child_path = path.empty() ? key : path + "." + key;
		lua_pushstring(L, key.c_str());     // [new, key]
		lua_pushvalue(L, -1);               // [new, key, key]
		lua_rawget(L, shape_idx);           // [new, key, shape_val]
		const int shape_val_idx = lua_gettop(L);
		if (!default_from_shape(ctx, shape_val_idx, child_path, depth + 1, true)) {
			lua_pop(L, 2); // shape_val, key
			return false;
		}
		lua_remove(L, shape_val_idx); // [new, key, default]
		lua_rawset(L, new_idx);      // new[key] = default; [new]
	}
	return true;
}

// The undeclared-key sweep shared by the named branch: every key state
// actually has must be one of `keys`, or the data would be silently orphaned.
bool refuse_undeclared(shape_walk_ctx &ctx, int state_idx, const std::vector<std::string> &keys,
	const std::string &path)
{
	lua_State *L = ctx.L;
	lua_pushnil(L);
	while (lua_next(L, state_idx) != 0) {
		const std::string kstr = key_to_string(L, -2);
		lua_pop(L, 1); // value
		if (std::find(keys.begin(), keys.end(), kstr) != keys.end()) {
			continue; // key kept on stack for lua_next
		}
		lua_pop(L, 1); // key
		ctx.refused = true;
		ctx.refuse_path = path.empty() ? kstr : path + "." + kstr;
		ctx.refuse_message = "not declared";
		return false;
	}
	return true;
}

// A shape table with named keys: each declared key is checked (and, if
// absent, inserted) by name; anything state holds outside that set refuses.
bool walk_named(shape_walk_ctx &ctx, int shape_idx, int state_idx, const std::vector<std::string> &keys,
	const std::string &path, int depth)
{
	lua_State *L = ctx.L;
	for (const auto &key : keys) {
		const std::string child_path = path.empty() ? key : path + "." + key;
		lua_pushstring(L, key.c_str());
		lua_rawget(L, shape_idx);
		const int shape_val_idx = lua_gettop(L);
		const int shape_type = lua_type(L, shape_val_idx);
		if (shape_type != LUA_TBOOLEAN && shape_type != LUA_TNUMBER && shape_type != LUA_TSTRING &&
			shape_type != LUA_TTABLE) {
			lua_pop(L, 1);
			ctx.refused = true;
			ctx.refuse_path = child_path;
			ctx.refuse_message = "state_shape declares an unsupported value";
			return false;
		}

		lua_pushstring(L, key.c_str());
		lua_rawget(L, state_idx);
		const int state_val_idx = lua_gettop(L);
		const int state_type = lua_type(L, state_val_idx);

		if (state_type == LUA_TNIL) {
			lua_pop(L, 1); // nil
			const bool ok = handle_missing(ctx, shape_val_idx, state_idx, key, child_path, depth + 1);
			lua_pop(L, 1); // shape_val
			if (!ok) {
				return false;
			}
			continue;
		}

		if (shape_type == LUA_TTABLE) {
			if (state_type != LUA_TTABLE) {
				ctx.refused = true;
				ctx.refuse_path = child_path;
				ctx.refuse_message = std::string("expected table, stored ") + lua_typename(L, state_type);
				lua_pop(L, 2);
				return false;
			}
			const bool ok = walk_table(ctx, shape_val_idx, state_val_idx, child_path, depth + 1);
			lua_pop(L, 2);
			if (!ok) {
				return false;
			}
			continue;
		}

		if (shape_type != state_type) {
			ctx.refused = true;
			ctx.refuse_path = child_path;
			ctx.refuse_message =
				std::string("expected ") + lua_typename(L, shape_type) + ", stored " + lua_typename(L, state_type);
			lua_pop(L, 2);
			return false;
		}
		lua_pop(L, 2);
	}

	return refuse_undeclared(ctx, state_idx, keys, path);
}

// An open collection: every key state actually has is checked against the
// one shape under "*", by value, never by name - that is what lets a chunk
// declare `enemies` without naming every id.
bool walk_open(shape_walk_ctx &ctx, int shape_idx, int state_idx, const std::string &path, int depth)
{
	lua_State *L = ctx.L;
	lua_pushstring(L, "*");
	lua_rawget(L, shape_idx);
	const int entry_shape_idx = lua_gettop(L);
	const int entry_type = lua_type(L, entry_shape_idx);
	if (entry_type != LUA_TBOOLEAN && entry_type != LUA_TNUMBER && entry_type != LUA_TSTRING &&
		entry_type != LUA_TTABLE) {
		lua_pop(L, 1);
		ctx.refused = true;
		ctx.refuse_path = path.empty() ? "*" : path + ".*";
		ctx.refuse_message = "state_shape declares an unsupported value";
		return false;
	}

	lua_pushnil(L);
	while (lua_next(L, state_idx) != 0) {
		const std::string kstr = key_to_string(L, -2);
		const std::string child_path = path.empty() ? kstr : path + "." + kstr;
		const int val_idx = lua_gettop(L);
		const int val_type = lua_type(L, val_idx);
		bool ok = true;
		if (entry_type == LUA_TTABLE) {
			if (val_type != LUA_TTABLE) {
				ctx.refused = true;
				ctx.refuse_path = child_path;
				ctx.refuse_message = std::string("expected table, stored ") + lua_typename(L, val_type);
				ok = false;
			} else {
				ok = walk_table(ctx, entry_shape_idx, val_idx, child_path, depth + 1);
			}
		} else if (val_type != entry_type) {
			ctx.refused = true;
			ctx.refuse_path = child_path;
			ctx.refuse_message =
				std::string("expected ") + lua_typename(L, entry_type) + ", stored " + lua_typename(L, val_type);
			ok = false;
		}
		lua_pop(L, 1); // value; key stays for lua_next
		if (!ok) {
			lua_pop(L, 2); // key, entry_shape
			return false;
		}
	}
	lua_pop(L, 1); // entry_shape
	return true;
}

} // namespace

// The one entry the sibling translation unit calls (rv_pccl_luajit_shape.cpp),
// so it alone has external linkage; everything above is this file's own.
//
// Both shape and state at this level are tables. Depth/node caps and the
// diamond check apply uniformly before deciding named vs. open collection.
bool walk_table(shape_walk_ctx &ctx, int shape_idx, int state_idx, const std::string &path, int depth)
{
	lua_State *L = ctx.L;
	if (depth > kShapeMaxDepth) {
		ctx.refused = true;
		ctx.refuse_path = path;
		ctx.refuse_message = "state_shape nested too deep";
		return false;
	}
	if (++ctx.nodes > kShapeMaxNodes) {
		ctx.refused = true;
		ctx.refuse_path = path;
		ctx.refuse_message = "state_shape has too many fields";
		return false;
	}

	const void *sp = lua_topointer(L, shape_idx);
	if (std::find(ctx.shape_seen.begin(), ctx.shape_seen.end(), sp) != ctx.shape_seen.end()) {
		ctx.refused = true;
		ctx.refuse_path = path;
		ctx.refuse_message = "state_shape reuses the same table on two paths";
		return false;
	}
	ctx.shape_seen.push_back(sp);

	const void *stp = lua_topointer(L, state_idx);
	if (std::find(ctx.state_seen.begin(), ctx.state_seen.end(), stp) != ctx.state_seen.end()) {
		ctx.refused = true;
		ctx.refuse_path = path;
		ctx.refuse_message = "state reuses the same table on two paths";
		return false;
	}
	ctx.state_seen.push_back(stp);

	std::vector<std::string> keys;
	lua_pushnil(L);
	while (lua_next(L, shape_idx) != 0) {
		if (lua_type(L, -2) != LUA_TSTRING) {
			lua_pop(L, 2);
			ctx.refused = true;
			ctx.refuse_path = path;
			ctx.refuse_message = "state_shape key must be a string";
			return false;
		}
		keys.emplace_back(lua_tostring(L, -2));
		lua_pop(L, 1);
	}

	if (keys.size() == 1 && keys[0] == "*") {
		return walk_open(ctx, shape_idx, state_idx, path, depth);
	}
	return walk_named(ctx, shape_idx, state_idx, keys, path, depth);
}

} // namespace rv_3dmppc
