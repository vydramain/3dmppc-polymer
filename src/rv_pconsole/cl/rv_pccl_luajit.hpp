// The console's rv_cl implementation: the script machine. Counterpart of
// rv_pccv for rv_cv and rv_pcca for rv_ca - the contract is opaque C, the
// concrete class lives here.
//
// lua.hpp is the PDK boundary and is confined to
// src/rv_pconsole/cl/rv_pccl_luajit* - the five .cpp files of this one class
// and their detail header. Nothing outside that set may include it. This
// header is not in that set: a pointer to an incomplete type needs nothing
// more than the forward declaration below, so nothing here pulls it in.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
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
    // not included by this header), and a plausible-looking 0 would read as a
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
    // cannot take the game's data with it. The entry chunk reaches it as
    // `state`, resolved through entry_env_ref_ below - it is deliberately NOT
    // a field of the real globals table, because _G belongs to no chunk and a
    // required module sharing a global with the entry would collide the
    // moment a disc raises one.
    //
    // 0 means "not created yet" for the same reason chunk_slot has no default:
    // LUA_NOREF cannot be named here. luaL_ref never hands out 0.
    int state_ref_ = 0;

    // What the console remembers about `state`'s own shape: a dotted path
    // ("enemies.3.hp") to the lua_type() tag it held the last time
    // capture_state_shape_ accepted it. Filled once, right after the entry
    // chunk's first disc_initialize call has returned - the moment the script
    // has finished setting `state` up - and replaced wholesale by every
    // accepted reload after that; see capture_state_shape_'s own comment for
    // why a replacement, not a merge. Empty before that first capture, which
    // is also how shape_captured_ below is redundant in principle but not
    // worth removing: it says so without a lookup.
    std::map<std::string, int> state_shape_;

    // Guards capture_state_shape_(initial=true) so the boot capture runs
    // exactly once, on the FIRST script_call that names "disc_initialize" on
    // the entry chunk - a disc calls that hook exactly once by contract
    // (pdklib/rv_dscript/rv_dscript.hpp), but this is a one-shot latch rather
    // than trusting that from here.
    bool shape_captured_ = false;

    // The entry chunk's own environment (its lua_setfenv target), reused for
    // every raise of the entry - the initial one and every reload candidate
    // alike, since it is the same `state` either way. A plain table with one
    // metamethod: __index falls through to the real globals table, so `pdk`,
    // `print`, `require` and the opened stdlib still resolve, but there is no
    // __newindex, so a write the script never declared `local` lands in THIS
    // table and never touches _G. `state` is set into it once, in the
    // constructor, right after state_ref_ exists - the same table object for
    // the machine's whole life, so handing it to a new candidate is nothing
    // more than pointing that candidate's own closure at this one table.
    // Never used for a module raised through reload_module_bytes_: a module
    // keeps the real globals table it always had, and has no state of its
    // own to reach.
    int entry_env_ref_ = 0;

    int loaded_ref_ = 0; // registry ref: module name -> the table require returned
    int invoke_ref_ = 0; // registry ref: protected_invoke_, made once (see protected_call_)

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

    int64_t revision_ = 0;    // successful entry reloads
    uint64_t entry_hash_ = 0; // FNV-1a of the bytes the entry is running

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

    // The console's own global `require`. Reads name.lua/name.luac off the
    // drive (rv_pccl_luajit_require.cpp); the stock package library stays
    // closed, same reason io/os do.
    static int require_(lua_State *L);

    // The one C closure every protected console call enters through: argument 1 is
    // the real trampoline as a light userdata, the rest are its own arguments.
    static int protected_invoke_(lua_State *L);

    // Runs `fn(L)` with `args` as its light-userdata argument 1, under lua_pcall.
    // The closure comes out of the registry, so nothing is allocated before the
    // pcall covers it - the call still answers on a heap the game has run out.
    // Returns lua_pcall's result; on failure the error object is left on top.
    int protected_call_(int (*fn)(lua_State *), void *args);

    // A count hook installed for the duration of a reload only. Without it a
    // `while true do end` in a candidate's body hangs the frame loop, and since
    // SIGINT only sets a flag the loop is no longer reading, the console would
    // need SIGKILL - losing the very session the developer is working in.
    static void insn_hook(lua_State *L, struct lua_Debug *ar);

    // The ceiling armed around every script_call: RV_PCCL_INSN_CEILING in a
    // development build, 0 (no guard) in a player build. Defined in the
    // dev-capability slot (rv_pccl_luajit_reload_devtools.cpp / _reload_standard.cpp).
    static int hook_insn_ceiling_();

    // Compile, run the body and demand a module table, under the instruction
    // ceiling. Returns RV_OK and a registry ref in `ref_out`, or a negative
    // rv_err with `report` naming the phase. Gives out NO handle: a reload must
    // not grow the handle table, or a long session would leave one dead slot
    // per keystroke.
    //
    // `is_entry` points the freshly compiled closure's environment at
    // entry_env_ref_ BEFORE its body runs, so every hook the body defines
    // (disc_initialize, frame_update, ...) inherits that same environment -
    // the ordinary Lua 5.1 rule that a nested function starts with whatever
    // environment its creator already has. False for a module
    // (reload_module_bytes_): a module keeps the real globals table it
    // always had and has no state of its own to reach.
    int64_t raise_(const void *bytecode, int64_t size, const char *name, int &ref_out,
        rv_pccl_reload_report &report, bool is_entry);

    // Builds the shape the console remembers from whatever `state` actually
    // holds right now - see the definition for the boot/reload split
    // (`initial`) and why a fresh build simply REPLACES state_shape_ instead
    // of being merged into it. The value-legality, depth and node protections
    // capture_walk still enforces can refuse a boot or a reload here; on a
    // reload the caller (reload_entry_bytes_) is the one that makes a
    // refusal cost nothing, via snapshot_state_/restore_state_ below.
    int64_t capture_state_shape_(bool initial, rv_pccl_reload_report &report);

    // Runs capture_walk under lua_pcall: looking a key up as a string INTERNS
    // it, and interning allocates, so a machine that has run its script heap
    // out must turn that allocation failure into a catchable error instead of
    // reaching the panic handler. Argument 1 is a capture_call_args*
    // (rv_pccl_luajit_shape.cpp).
    static int shape_capture_trampoline_(lua_State *L);

    // Deep-copies the live state table into a fresh one and hands back its
    // registry ref, so a reload candidate about to run can be undone even
    // though its body already had `state` in reach before this console ever
    // gets to check it. Negative on an allocation failure or on hitting the
    // walk's own depth/node bound - either way the reload refuses before the
    // candidate is even raised, and nothing has been snapshotted to clean up.
    int64_t snapshot_state_(int &ref_out, rv_pccl_reload_report &report);
    static int state_snapshot_trampoline_(lua_State *L);

    // The other half of snapshot_state_: empties the live state table in
    // place (so every existing reference to it, in particular
    // entry_env_ref_'s `state` field, keeps pointing at a live table) and
    // refills it from `snapshot_ref`, then releases that ref. Called only
    // when a reload candidate's shape capture refused - the one case where
    // whatever the candidate's body wrote to state must not survive it.
    void restore_state_(int snapshot_ref);
    static int state_restore_trampoline_(lua_State *L);

    // The path walk, run under lua_pcall for the same reason: looking a key up
    // INTERNS it, and interning allocates, so a machine that has run its script
    // heap out turns the console's own inspection into a raise. Without this it
    // reaches the panic handler and the process ends - the one command that
    // exists to find out what went wrong would be the one that kills the run.
    // Argument 1 is a state_walk_args* (rv_pccl_luajit_state.cpp). Shared by
    // state_get and state_keys: the resolved value is left on top of the lua
    // stack for the caller to read (and to walk children of, for state_keys).
    static int state_walk_trampoline_(lua_State *L);

    // The phase name a failure deserves: the instruction ceiling and the memory
    // budget both surface as an ordinary lua error, so without the two flags
    // they would be reported as whatever the caller happened to be doing.
    const char *phase_of(const char *phase) const;

    // Shared by both entry reload forms once the bytes are in hand.
    int64_t reload_entry_bytes_(const void *bytecode, int64_t size, const char *name,
        rv_pccl_reload_report &report);

    // Turns a require() module name into the asset name require_ would read:
    // 0 ok (out filled), 1 not a module name, 2 the entry is neither .lua nor .luac.
    int module_asset_(const char *name, char *out, std::size_t cap) const;

    // The refusals a module reload owes before any bytes are read: a call in
    // flight, a name that is not a module, a module nobody has required. On
    // success `asset` holds the asset name and `ref_out` a registry ref to the
    // running module table, which the caller releases.
    int64_t module_find_(const char *name, char *asset, std::size_t cap, int &ref_out,
        rv_pccl_reload_report &report);

    // Shared by both module reload forms once the bytes are in hand.
    int64_t reload_module_bytes_(const char *name, const void *bytecode, int64_t size,
        rv_pccl_reload_report &report);

    // Updates the tables `old_ref` holds to carry `new_ref`'s contents in
    // place, instead of swapping the reference - anything already holding a
    // reference into the old tables keeps it and runs the new code. Defined
    // only in rv_pccl_luajit_patch_devtools.cpp (dev-only, see CMakeLists.txt); a
    // player build never calls this.
    int64_t patch_in_place_(int old_ref, int new_ref, rv_pccl_reload_report &report);

    // Runs the whole pair/count/patch walk under one lua_pcall: only an
    // allocation failure can make it fail from the caller's point of view.
    // Argument 1 is a patch_call_args* (rv_pccl_luajit_patch_devtools.cpp).
    static int patch_trampoline_(lua_State *L);

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
    int64_t script_reload_module(const char *name, const void *bytecode, int64_t size,
        rv_pccl_reload_report &report) override;
    int64_t script_reload_module_from_drive(const char *name, rv_pccl_reload_report &report) override;
    int64_t state_get(const std::vector<std::string> &path, rv_pccl_value &out) override;
    int64_t state_keys(const std::vector<std::string> &path, rv_pccl_value &target,
        std::vector<rv_pccl_key> &out) override;
    int64_t state_collect(int64_t *used_out) override;
    void script_status(rv_pccl_status &out) const override;
};

} // namespace rv_3dmppc
