// What the machine says about itself: the rerouted print(), reading one field of
// the state table, the collector, and status.
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

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
// Carries the walk across the protected-call boundary, the same shape
// shape_call_args uses: no C++ return value and no exception may cross it.
// `keys` is null for a plain state_get; set, it asks the trampoline to also
// list the resolved value's children when it is a table.
struct state_walk_args {
    rv_pccl_luajit *self = nullptr;
    const std::vector<std::string> *path = nullptr;
    rv_pccl_value *out = nullptr;
    std::vector<rv_pccl_key> *keys = nullptr;
};

// Is `s` the canonical decimal spelling of an integer a lua number holds
// exactly: "0", or an optional '-' then a digit 1-9 then up to 15 more digits,
// within 2^53? Anything else is not a number a segment can mean unambiguously
// (leading zeros, "+1", a value that would round onto a neighbouring key).
bool canonical_int(const std::string &s, int64_t &out)
{
    if (s == "0") {
        out = 0;
        return true;
    }
    std::size_t i = (s.size() > 0 && s[0] == '-') ? 1 : 0;
    if (i >= s.size() || s[i] < '1' || s[i] > '9') {
        return false;
    }
    if (s.size() - i > 16) {
        return false;
    }
    for (std::size_t j = i + 1; j < s.size(); ++j) {
        if (s[j] < '0' || s[j] > '9') {
            return false;
        }
    }
    out = std::stoll(s);
    return out >= -9007199254740992LL && out <= 9007199254740992LL;
}

// The RV_CL_TYPE_* of the value at `idx`, or -1 for nil - the mapping
// state_get and state_keys both answer with.
int64_t type_of(lua_State *L, int idx)
{
    switch (lua_type(L, idx)) {
    case LUA_TNIL:
        return -1;
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
    default:
        return RV_CL_TYPE_OTHER;
    }
}

// Fills `out` from the value at absolute index `idx`: the same fields
// state_get has always reported, plus `count` (a full lua_next pass) when it
// is a table. Leaves the stack exactly as it found it.
void fill_value(lua_State *L, int idx, rv_pccl_value &out)
{
    out.type = type_of(L, idx);
    switch (out.type) {
    case RV_CL_TYPE_BOOLEAN:
        out.boolean = lua_toboolean(L, idx) != 0;
        break;
    case RV_CL_TYPE_NUMBER:
        out.number = static_cast<double>(lua_tonumber(L, idx));
        break;
    case RV_CL_TYPE_STRING: {
        std::size_t len = 0;
        const char *text = lua_tolstring(L, idx, &len);
        out.bytes.assign(text, len); // raw: it may hold NUL and invalid UTF-8
        break;
    }
    case RV_CL_TYPE_TABLE: {
        int64_t n = 0;
        lua_pushnil(L);
        while (lua_next(L, idx) != 0) {
            lua_pop(L, 1); // the value; the key stays for the next lua_next
            ++n;
        }
        out.count = n;
        break;
    }
    default:
        break;
    }
}

// Every child of the table at absolute index `idx`, in lua_next order.
void table_children(lua_State *L, int idx, std::vector<rv_pccl_key> &out)
{
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        rv_pccl_key k;
        if (lua_type(L, -2) == LUA_TSTRING) {
            std::size_t len = 0;
            const char *text = lua_tolstring(L, -2, &len);
            k.kind = 's';
            k.name.assign(text, len);
        } else if (lua_type(L, -2) == LUA_TNUMBER) {
            const double v = lua_tonumber(L, -2);
            if (v == std::floor(v) && std::fabs(v) <= 9007199254740992.0 /* 2^53 */) {
                k.kind = 'i';
                k.name = std::to_string(static_cast<int64_t>(v));
            }
        }
        k.type = type_of(L, -1);
        out.push_back(std::move(k));
        lua_pop(L, 1); // the value; the key stays for the next lua_next
    }
}
} // namespace

// RAW walk from the persistent state table, one rawget per segment. No
// metatable is consulted, so inspecting state can never run script code: a
// channel that evaluates is a channel that can be asked to do anything, and
// this one is only allowed to look. A segment that misses as a string key and
// spells a canonical decimal integer is tried again as a number key - the one
// case a table built with numeric indices needs. Once the walk leaves the
// table shape, every further segment answers nil the same way.
//
// Under lua_pcall because lua_pushlstring INTERNS the segment, and interning
// allocates from the script heap: on a machine that has already run that heap
// out, the read itself raises. Unprotected that reaches lua_atpanic and ends
// the process - so the command whose whole job is to explain a broken run
// would be the command that destroys it. Protected, it is an ordinary
// RV_ERR_NOMEM the channel answers with. Shared by state_get and state_keys:
// `args->keys` null skips the child listing.
int rv_pccl_luajit::state_walk_trampoline_(lua_State *L)
{
    auto *args = static_cast<state_walk_args *>(lua_touserdata(L, 1));
    rv_pccl_luajit *self = args->self;

    lua_rawgeti(L, LUA_REGISTRYINDEX, self->state_ref_); // the walk's starting value
    for (const std::string &seg : *args->path) {
        if (lua_type(L, -1) != LUA_TTABLE) {
            lua_pop(L, 1);
            lua_pushnil(L);
            continue;
        }
        const int table_idx = lua_gettop(L);
        lua_pushlstring(L, seg.data(), seg.size()); // the allocating step this pcall exists for
        lua_rawget(L, table_idx);
        int64_t n = 0;
        if (lua_isnil(L, -1) && canonical_int(seg, n)) {
            lua_pop(L, 1);
            lua_pushnumber(L, static_cast<lua_Number>(n));
            lua_rawget(L, table_idx);
        }
        lua_remove(L, table_idx); // drop the old value, the new one is now on top
    }

    const int value_idx = lua_gettop(L);
    fill_value(L, value_idx, *args->out);
    if (args->keys != nullptr && args->out->type == RV_CL_TYPE_TABLE) {
        table_children(L, value_idx, *args->keys);
    }
    lua_pop(L, 1);
    return 0;
}

int64_t rv_pccl_luajit::state_get(const std::vector<std::string> &path, rv_pccl_value &out)
{
    out = rv_pccl_value{};
    if (path.empty() || path.size() > RV_PCCL_STATE_PATH_MAX) {
        return RV_ERR_INVAL;
    }
    [[maybe_unused]] const int top = lua_gettop(L_);

    state_walk_args args;
    args.self = this;
    args.path = &path;
    args.out = &out;

    if (protected_call_(state_walk_trampoline_, &args) != 0) {
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

int64_t rv_pccl_luajit::state_keys(const std::vector<std::string> &path, rv_pccl_value &target,
    std::vector<rv_pccl_key> &out)
{
    target = rv_pccl_value{};
    out.clear();
    if (path.size() > RV_PCCL_STATE_PATH_MAX) {
        return RV_ERR_INVAL;
    }
    [[maybe_unused]] const int top = lua_gettop(L_);

    state_walk_args args;
    args.self = this;
    args.path = &path;
    args.out = &target;
    args.keys = &out;

    if (protected_call_(state_walk_trampoline_, &args) != 0) {
        target = rv_pccl_value{};
        out.clear();
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
