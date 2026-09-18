// Internals shared by the rv_pccl_luajit translation units
// (rv_pccl_luajit.cpp, _stack.cpp, _chunks.cpp, _gate.cpp, _reload.cpp,
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

#include "lua.hpp"

namespace rv_3dmppc
{
// How many VM instructions guarded script code gets: a reload candidate's
// body and attach(), and, in a development build, every hook call the console
// makes into a chunk. Not a timeout: a count hook cannot bound the parser, a C
// call or an FFI call, and it says nothing about wall clock. What it does bound
// is the ordinary mistake this ceiling exists for - a loop with no exit in code
// the developer is in the middle of editing. Generous enough that a
// legitimately heavy attach (building a large table) never meets it.
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
// is what makes the guard safe to nest: reload arms the ceiling around both the
// body and attach(), and the raise_ inside it arms its own - the inner
// restoration must not disarm the outer one.
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

// Bound the walk itself, independent of what either tree contains: a
// malformed or adversarial state_shape/state pair must not be able to make
// this run unbounded, the same reason the reload path bounds VM instructions.
// The walk is plain C++ and executes no bytecode, so the instruction ceiling
// cannot see it - these are the only bounds it has.
//
// One unit is one table visited OR one key examined: counting tables alone
// left a flat table with a huge key count costing one unit. The number is set
// so it cannot fire before [budget.pccl] script_memory_size does - a table
// entry costs LuaJIT tens of bytes, so a 256 KiB script heap runs out at
// roughly eight thousand entries, and a cap that refused a state table the
// machine was willing to hold would diagnose the wrong problem.
constexpr int kShapeMaxDepth = 16;
constexpr int kShapeMaxNodes = 16384;

// One walk's working state: which pass (validate or apply), the caps, the
// first refusal found (a walk stops at the first one), and - APPLY only -
// where inserted keys get recorded so a later attach() refusal can undo them.
struct shape_walk_ctx {
    lua_State *L = nullptr;
    bool apply = false;
    int nodes = 0;
    bool refused = false;
    std::string refuse_path;
    std::string refuse_message;
    std::vector<const void *> shape_path;  // the shape tables on the CURRENT path: catches a cycle, permits a reused template
    std::vector<const void *> state_seen;  // every state table already visited: catches a diamond in the game's data
    std::vector<rv_pccl_luajit::state_shape_insert> *inserted = nullptr;
};

// Walk function declarations: the structural walk functions from _shapewalk.cpp
// that are called by shape_trampoline_ in _shape.cpp.
bool walk_table(shape_walk_ctx &ctx, int shape_idx, int state_idx, const std::string &path, int depth);
} // namespace rv_3dmppc
