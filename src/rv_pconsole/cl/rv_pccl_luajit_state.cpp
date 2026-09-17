// What the machine says about itself: the rerouted print(), reading one field of
// the state table, the collector, and status.
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include <cassert>
#include <cstddef>
#include <string>

#include "lua.hpp"

#include "pdk/cl/rv_cl.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

// base print() writes to stdout, and stdout is the development channel's answer
// stream. Routing it here is not a preference: a chunk printing one line there
// would splice text into a reply the editor is parsing. The stock semantics are
// kept otherwise - every argument through the global tostring, tab separated -
// so a script's own diagnostics keep working, they just land where every other
// diagnostic of this console already lands.
int rv_pccl_luajit::print_to_log(lua_State *L)
{
    const int argc = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= argc; ++i) {
        // The global tostring, exactly as base print does, so __tostring is
        // honoured. It may raise; this runs inside the caller's pcall.
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        std::size_t len = 0;
        const char *text = lua_tolstring(L, -1, &len);
        if (text != nullptr) {
            if (i > 1) {
                line.push_back('\t');
            }
            line.append(text, len);
        }
        lua_pop(L, 1);
    }
    // Escaped: script text is the one string in this process most likely to
    // carry a newline or an ANSI escape, and a log line a chunk can forge is
    // a log nobody can trust.
    RV_LOG_INFO("lua", "{}", rv_pdklib::rv_log_escape(line.c_str(), 512));
    return 0;
}

namespace
{
// Carries the read across the protected-call boundary, the same shape
// shape_call_args uses: no C++ return value and no exception may cross it.
struct state_get_args {
    rv_pccl_luajit *self = nullptr;
    const char *key = nullptr;
    rv_pccl_value *out = nullptr;
};
} // namespace

// RAW read of one top-level field. No metatable is consulted, so inspecting
// state can never run script code: a channel that evaluates is a channel that
// can be asked to do anything, and this one is only allowed to look.
//
// Under lua_pcall because lua_pushstring INTERNS the key, and interning
// allocates from the script heap: on a machine that has already run that heap
// out, the read itself raises. Unprotected that reaches lua_atpanic and ends
// the process - so the command whose whole job is to explain a broken run
// would be the command that destroys it. Protected, it is an ordinary
// RV_ERR_NOMEM the channel answers with.
int rv_pccl_luajit::state_get_trampoline_(lua_State *L)
{
    auto *args = static_cast<state_get_args *>(lua_touserdata(L, 1));
    rv_pccl_luajit *self = args->self;
    rv_pccl_value &out = *args->out;

    lua_rawgeti(L, LUA_REGISTRYINDEX, self->state_ref_);
    lua_pushstring(L, args->key); // the allocating step this pcall exists for
    lua_rawget(L, -2);

    switch (lua_type(L, -1)) {
    case LUA_TNIL:
        // A lua table stores no nil, so "absent" and "nil" are one fact and get
        // one answer.
        out.type = -1;
        break;
    case LUA_TBOOLEAN:
        out.type = RV_CL_TYPE_BOOLEAN;
        out.boolean = lua_toboolean(L, -1) != 0;
        break;
    case LUA_TNUMBER:
        out.type = RV_CL_TYPE_NUMBER;
        out.number = static_cast<double>(lua_tonumber(L, -1));
        break;
    case LUA_TSTRING: {
        out.type = RV_CL_TYPE_STRING;
        std::size_t len = 0;
        const char *text = lua_tolstring(L, -1, &len);
        out.bytes.assign(text, len); // raw: it may hold NUL and invalid UTF-8
        break;
    }
    case LUA_TFUNCTION:
        out.type = RV_CL_TYPE_FUNCTION;
        break;
    case LUA_TTABLE:
        out.type = RV_CL_TYPE_TABLE;
        break;
    default:
        out.type = RV_CL_TYPE_OTHER;
        break;
    }

    lua_pop(L, 2);
    return 0;
}

int64_t rv_pccl_luajit::state_get(const char *key, rv_pccl_value &out)
{
    if (key == nullptr) {
        return RV_ERR_INVAL;
    }
    [[maybe_unused]] const int top = lua_gettop(L_);
    out = rv_pccl_value{};

    state_get_args args;
    args.self = this;
    args.key = key;
    args.out = &out;

    lua_pushcfunction(L_, state_get_trampoline_);
    lua_pushlightuserdata(L_, &args);
    if (lua_pcall(L_, 1, 0, 0) != 0) {
        // The heap is out; `out` is whatever the walk had filled in, so it is
        // reset rather than half-reported.
        out = rv_pccl_value{};
        lua_pop(L_, 1);
        assert(lua_gettop(L_) == top);
        return RV_ERR_NOMEM;
    }
    assert(lua_gettop(L_) == top);
    return RV_OK;
}

int64_t rv_pccl_luajit::state_collect(int64_t *used_out)
{
    lua_gc(L_, LUA_GCCOLLECT, 0);
    if (used_out != nullptr) {
        *used_out = used_;
    }
    return RV_OK;
}

void rv_pccl_luajit::script_status(rv_pccl_status &out) const
{
    out.revision = revision_;
    out.hash = entry_hash_;
    out.used = used_;
    out.budget = budget_;
    out.slots = static_cast<int64_t>(chunks_.size());
    out.error_seq = error_seq_;
    out.reloadable = entry_ >= 0 && entry_attach_;
    out.error = error_text_;
}

} // namespace rv_3dmppc
