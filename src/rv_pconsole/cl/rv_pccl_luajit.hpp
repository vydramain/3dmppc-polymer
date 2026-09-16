// The console's rv_cl implementation: the script machine. Counterpart of
// rv_pccv for rv_cv and rv_pcca for rv_ca - the contract is opaque C, the
// concrete class lives here.
//
// lua.hpp is the PDK boundary and is included by exactly ONE file in the
// whole project: rv_pccl_luajit.cpp. A pointer to an incomplete type needs
// nothing more than the forward declaration below, so nothing here pulls it in.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cl/rv_pccl.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

struct lua_State;
struct lua_Debug;

namespace rv_3dmppc
{

// The lua machine. Only ever built when the disc declared a lua machine
// (rv_pccl_conf.hpp: script_memory_size > 0) - a factory upstream guarantees
// that before this class exists at all, so every method below can assume a
// budgeted machine was asked for.
class rv_pccl_luajit final : public rv_pccl
{
private:
    rv_pccl_conf conf_;
    rv_pccd &cd_; // BORROWED. Reads the entry asset out of the archive.

    lua_State *L_ = nullptr; // the whole machine; null means bring-up failed

    int64_t budget_ = 0; // conf_.script_memory_size, cached for the allocator
    int64_t used_ = 0;   // bytes the allocator currently has outstanding

    // Handle table, the same idea as rv_pccd's resource table - a
    // handle is an index into chunks_, and it is NEVER reused: LUA_NOREF
    // marks a slot script_free() emptied, so a stale handle finds nothing
    // rather than landing on somebody else's chunk.
    //
    // The REF is what a reload swaps: the handle keeps its index, the slot gets
    // a different registry reference, and the disc goes on calling the same
    // number. The name travels with it so the log line after a reload says which
    // file the code came from, and so a future multi-chunk reload has something
    // to select on.
    // No default for `ref`: LUA_NOREF is not visible in this header (lua.hpp is
    // included by exactly one .cpp), and a plausible-looking 0 would read as a
    // valid reference. Every slot is brace-initialised with a real one.
    struct chunk_slot {
        int ref;
        std::string name;
    };
    std::vector<chunk_slot> chunks_;

    int64_t entry_ = -1; // memoised script_entry() handle; -1 = not raised yet

    // --- the development runtime ------------------------------------------

    // The persistent state table, owned by this machine and held in the
    // registry for the whole run. This is the whole of "code != state": the
    // table outlives every chunk that ever sees it, so replacing the code
    // cannot take the game's data with it. Chunks reach it only as attach()'s
    // argument - it is deliberately NOT a global, because _G belongs to no
    // chunk and two chunks sharing a global would collide the moment a disc
    // raises a second one.
    //
    // 0 means "not created yet" for the same reason chunk_slot has no default:
    // LUA_NOREF cannot be named here. luaL_ref never hands out 0.
    int state_ref_ = 0;

    // How deep we are inside script code right now. A COUNTER and not a flag:
    // a hook that calls back into another hook would let a flag clear itself on
    // the inner return, and a reload during the outer call would then be
    // allowed to swap code that is still on the stack. Anything that replaces
    // or frees a chunk refuses while this is non-zero.
    int call_depth_ = 0;

    // Set by rv_alloc when it refuses growth. Lua turns a null allocation into
    // an ordinary catchable error, so without this flag "out of script memory"
    // and "your chunk threw" would arrive as the same failed pcall and the
    // report would have to guess from the message text.
    bool oom_ = false;

    // Set by the count hook below. The hook is static and has no `this`, but
    // the lua_State does: lua_getallocf hands back the userdata the state was
    // created with, which is this object (see rv_alloc). Without this flag an
    // interrupted chunk would arrive as an ordinary failed pcall and "your loop
    // never ended" would be reported as "your chunk threw".
    bool ceiling_hit_ = false;

    int64_t revision_ = 0;      // successful entry reloads
    uint64_t entry_hash_ = 0;   // FNV-1a of the bytes the entry is running
    bool entry_attach_ = false; // the entry chunk has attach(); reload needs it

    int64_t error_seq_ = 0;     // failed hook calls, ever; the console watches it
    std::string error_text_;    // the last one, for status

    // lua_Alloc for this machine: a realloc that refuses to push used_ past
    // budget_. `ud` is the rv_pccl_luajit the state was created with (lua_newstate).
    static void *rv_alloc(void *ud, void *ptr, size_t osize, size_t nsize);

    // Installed via lua_atpanic: by default an error raised outside every
    // pcall calls abort() and the console dies with nothing in the log.
    static int panic(lua_State *L);

    // Replaces the stock global print(). base print() writes to STDOUT, and
    // stdout is the development channel's answer stream - a chunk printing a
    // line there would splice text into a reply. Everything the console says
    // about its own work already goes to stderr through rv_logs, and so does
    // this.
    static int print_to_log(lua_State *L);

    // A count hook installed for the duration of a reload only. Without it a
    // `while true do end` in a candidate's body hangs the frame loop, and since
    // SIGINT only sets a flag the loop is no longer reading, the console would
    // need SIGKILL - losing the very session the developer is working in.
    static void insn_hook(lua_State *L, struct lua_Debug *ar);

    // Compile, run the body and demand a module table, under the instruction
    // ceiling. Returns RV_OK and a registry ref in `ref_out`, or a negative
    // rv_err with `report` naming the phase. Gives out NO handle: a reload must
    // not grow the handle table, or a long session would leave one dead slot
    // per keystroke.
    int64_t raise_(const void *bytecode, int64_t size, const char *name, int &ref_out,
        rv_pccl_reload_report &report);

    // The names a gate hook's three failures get. Passed in rather than derived
    // from the hook name, because the client branches on these tokens and they
    // are part of the protocol, not a formatting detail.
    struct gate_phases {
        const char *missing;
        const char *raised;
        const char *refused;
        // Returned something that is not a boolean at all. Parameterised like
        // the other three: a shared implementation that hardcoded one hook's
        // token would answer an asset request with an attach error, and the
        // client branches on these strings.
        const char *contract;
    };

    // Call a GATE HOOK on the chunk `ref` holds: one argument in, and
    // `true` or `false, "reason"` out. Two hooks share this shape - attach(),
    // which decides whether the new code accepts the old state, and
    // asset_changed(), which decides whether the game could take the refreshed
    // asset - so they share one implementation of the stack protocol rather
    // than two copies of the same twenty lines.
    //
    // `string_arg` null means "hand it the state table"; otherwise that string
    // is the argument. Raw lookup throughout, so a metatable cannot make the
    // console call something other than what it asked for.
    int64_t call_gate_(int ref, const char *hook, const char *string_arg,
        const gate_phases &phases, rv_pccl_reload_report &report);

    // attach(state): the one gate every reloadable chunk must have. A chunk that
    // returns false has REFUSED the state it was handed - the
    // incompatible-state answer - and that is a failed reload, not a crash.
    int64_t attach_(int ref, rv_pccl_reload_report &report);

    // Is there a function under `hook` in the table `ref` holds? Raw, same reason.
    bool has_hook_(int ref, const char *hook) const;

    // The phase name a failure deserves: the instruction ceiling and the memory
    // budget both surface as an ordinary lua error, so without the two flags
    // they would be reported as whatever the caller happened to be doing.
    const char *phase_of(const char *phase) const;

    // Shared by both reload forms once the bytes are in hand.
    int64_t reload_entry_bytes_(const void *bytecode, int64_t size, const char *name,
        rv_pccl_reload_report &report);

    // Builds the console<->script vocabulary: opens ffi, feeds it
    // rv_pdk_cdef, and turns rv_pdk_consts into the global `pdk` table (see
    // rv_pccl_luajit.cpp for the whole recipe and why it is not inlined in
    // the constructor). Returns false on ANY failure - a bad cdef, a symbol
    // the build never exported - and touches nothing that survives that: the
    // constructor closes L_ down on a false return, same as a failed
    // lua_newstate.
    bool bootstrap_pdk();

public:
    rv_pccl_luajit(const rv_pccl_conf &conf, rv_pccd &cd);
    ~rv_pccl_luajit() override;

    rv_pccl_luajit(const rv_pccl_luajit &) = delete;
    rv_pccl_luajit &operator=(const rv_pccl_luajit &) = delete;

    // The peak host bytes this class allocates for `budget`:
    // budget_ (script_memory_size), the lua_Alloc ceiling this class enforces
    // on the VM's own heap.
    static rv_pcbudget_cost evaluate(const rv_pdklib::rv_manifest_budget &budget);

    // A VM that failed to come up is the only FALSE: the console refuses to
    // boot on that, so no contract method below is ever reached with L_ null
    // - see the comment on that above valid()'s definition.
    bool valid() const override;

    int64_t script_load(const void *bytecode, int64_t size, const char *name) override;
    int64_t script_free(int64_t handle) override;
    int64_t script_entry() override;

    int64_t stack_push_nil() override;
    int64_t stack_push_boolean(bool value) override;
    int64_t stack_push_integer(int64_t value) override;
    int64_t stack_push_number(double value) override;
    int64_t stack_push_string(const char *text, int64_t length) override;
    int64_t stack_push_pointer(void *p) override;
    int64_t stack_drop(int64_t count) override;
    int64_t stack_count() override;

    int64_t value_type(int64_t index) override;
    int64_t value_boolean(int64_t index, bool *out) override;
    int64_t value_integer(int64_t index, int64_t *out) override;
    int64_t value_number(int64_t index, double *out) override;
    int64_t value_string(int64_t index, char *baddr, int64_t baddr_size) override;

    int64_t script_call(int64_t handle, const char *fname, int64_t argc, int64_t retc) override;

    int64_t script_reload_entry(const void *bytecode, int64_t size, const char *name,
        rv_pccl_reload_report &report) override;
    int64_t script_reload_entry_from_drive(rv_pccl_reload_report &report) override;
    int64_t script_asset_changed(const char *name, rv_pccl_reload_report &report) override;
    int64_t state_get(const char *key, rv_pccl_value &out) override;
    int64_t state_collect(int64_t *used_out) override;
    void script_status(rv_pccl_status &out) const override;
};

} // namespace rv_3dmppc
