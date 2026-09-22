// Internals shared by the rv_pccl_luajit translation units
// (rv_pccl_luajit.cpp, _stack.cpp, _chunks.cpp, _gate.cpp, _reload_devtools.cpp,
// _state.cpp, _shape.cpp, _shapewalk.cpp). The instruction ceiling and both
// guards are used from more than one of them, and duplicating a guard would
// mean two copies of the nesting rule its comment records - one of which
// would eventually stop matching the other.
//
// lua.hpp is the PDK boundary and is confined to
// src/rv_pconsole/cl/rv_pccl_luajit* - this header and the .cpp files of
// that one class. If it shows up anywhere else, the PDK boundary has leaked.
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "lua.hpp"

namespace rv_3dmppc
{
// How many VM instructions guarded script code gets: a reload candidate's
// body, and, in a development build, every hook call the console makes into a
// chunk. Not a timeout: a count hook cannot bound the parser, a C call or an
// FFI call, and it says nothing about wall clock. What it does bound is the
// ordinary mistake this ceiling exists for - a loop with no exit in code the
// developer is in the middle of editing. Generous enough that a legitimately
// heavy hook (building a large table) never meets it.
constexpr int RV_PCCL_INSN_CEILING = 50 * 1000 * 1000;

// FNV-1a, 64 bit. Identifies the bytes a chunk is running so `status` can say
// "this is still the code you sent"; it is not a security claim and nothing
// depends on it being hard to collide. rv_disc_hash is the ELF checksum and has
// a different job - conflating them would mean one number answering two
// questions.
inline uint64_t rv_pccl_fnv1a(const void *bytes, int64_t size)
{
    const unsigned char *p = static_cast<const unsigned char *>(bytes);
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (int64_t i = 0; i < size; ++i) {
        hash ^= p[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// Installs a hook and puts the PREVIOUS one back, rather than clearing it. That
// is what makes the guard safe to nest: a development build arms one ceiling
// around a whole script_call, and a hook that calls back into raise_ (e.g.
// rv_cl_script_load, raising another chunk) arms its own inside that - the
// inner restoration must not disarm the outer one.
class rv_pccl_insn_guard
{
public:
    rv_pccl_insn_guard(lua_State *L, lua_Hook hook, int count)
        : L_(L)
        , old_hook_(lua_gethook(L))
        , old_mask_(lua_gethookmask(L))
        , old_count_(lua_gethookcount(L))
    {
        lua_sethook(L_, hook, LUA_MASKCOUNT, count);
    }
    ~rv_pccl_insn_guard()
    {
        lua_sethook(L_, old_hook_, old_mask_, old_count_);
    }
    rv_pccl_insn_guard(const rv_pccl_insn_guard &) = delete;
    rv_pccl_insn_guard &operator=(const rv_pccl_insn_guard &) = delete;

private:
    lua_State *L_;
    lua_Hook old_hook_;
    int old_mask_;
    int old_count_;
};

// Balanced +1/-1 around anything that runs script code, so that a reload or a
// free arriving from the development channel can refuse while the interpreter
// is still on the stack. A guard object and not two bare statements: the pcall
// between them has an early-return path.
class rv_pccl_call_guard
{
public:
    explicit rv_pccl_call_guard(int &depth)
        : depth_(depth)
    {
        ++depth_;
    }
    ~rv_pccl_call_guard()
    {
        --depth_;
    }
    rv_pccl_call_guard(const rv_pccl_call_guard &) = delete;
    rv_pccl_call_guard &operator=(const rv_pccl_call_guard &) = delete;

private:
    int &depth_;
};

// Bound the walk itself, independent of what the live state table contains: a
// malformed or adversarial state must not be able to make this run unbounded,
// the same reason the reload path bounds VM instructions. The walk is plain
// C++ and executes no bytecode, so the instruction ceiling cannot see it -
// these are the only bounds it has. They also double as the only cycle guard
// this walk needs: a table that contains itself, directly or through a chain
// of others, simply recurses until kShapeMaxDepth refuses it - a dedicated
// cycle check bought nothing a depth cap did not already buy for free, once
// there was no longer a second, DECLARED tree whose own depth could otherwise
// run away independent of the live one.
//
// One unit is one table visited OR one key examined: counting tables alone
// left a flat table with a huge key count costing one unit. The number is set
// so it cannot fire before [budget.pccl] script_memory_size does - a table
// entry costs LuaJIT tens of bytes, so a 256 KiB script heap runs out at
// roughly eight thousand entries, and a cap that refused a state table the
// machine was willing to hold would diagnose the wrong problem.
constexpr int kShapeMaxDepth = 16;
constexpr int kShapeMaxNodes = 16384;

// One capture/compare walk's working state. `old_shape` is null at boot (there
// is nothing yet to compare against - every key found is simply new) and
// non-null on a reload (the shape remembered since the last accepted state);
// `fresh` collects the type of every key the walk actually finds, keyed by its
// dotted path, and REPLACES the remembered shape wholesale once the walk
// finishes without a refusal - that single replacement is what lets a key the
// new code stopped using drop out and a key it added join in, with no separate
// insert/remove bookkeeping. The type stored is a plain lua_type() tag.
struct shape_capture_ctx {
    lua_State *L = nullptr;
    bool initial = false;
    const std::map<std::string, int> *old_shape = nullptr;
    std::map<std::string, int> *fresh = nullptr;
    int nodes = 0;
    bool refused = false;
    std::string refuse_path;
    std::string refuse_message;
};

// The structural walk from _shapewalk.cpp, called by capture_state_shape_ in
// _shape.cpp.
bool capture_walk(shape_capture_ctx &ctx, int table_idx, const std::string &path, int depth);

// The in-place patch (dev slot): patch_trampoline_ in _patch_devtools.cpp runs the
// passes, _patchwalk_devtools.cpp holds the two walks it calls.
// One reachable table or function, counted once, is the unit both bounds are
// measured in - the same reasoning as kShapeMaxNodes: a malformed or merely
// very large candidate must refuse cleanly rather than run away.
constexpr int RV_PCCL_PATCH_NODE_MAX = 65536;
constexpr int RV_PCCL_PATCH_DEPTH_MAX = 64;

// Bookkeeping shared by the whole walk: ordinary lua tables used only as
// identity sets/maps (keyed by the value itself, looked up raw). MAP pairs a
// NEW table with the OLD table Pass 1 gave it. OLD_CLAIMED marks an old table
// already given to a pair - checked directly by Pass 2 as part of LIVE.
// PAIR_SEEN marks a new table Pass 1 visited but did not pair, so a cycle
// among unpaired new tables ends. SEEN marks a table/function Pass 2 counted,
// for its node budget and to stop at a cycle. LIVE marks a table Pass 2 must
// never enter: the persistent state and every loaded module (built once, see
// patch_trampoline_).
struct patch_ctx {
    lua_State *L;
    int map_idx;
    int old_claimed_idx;
    int pair_seen_idx;
    int seen_idx;
    int live_idx;
    int nodes = 0;
    bool refused = false;
    const char *refuse_message = nullptr;
};

bool patch_pair(patch_ctx &ctx, int o_idx, int n_idx, int depth);
bool patch_reach(patch_ctx &ctx, int n_idx, int depth);
} // namespace rv_3dmppc
