// The console's rv_cl implementation: the script machine. Counterpart of
// rv_pccv for rv_cv and rv_pcca for rv_ca — the contract is opaque C, the
// concrete class lives here and nothing above it inherits anything.
//
// lua.hpp is the PDK boundary and is included by exactly ONE file in the
// whole project: rv_pccl.cpp. A pointer to an incomplete type needs nothing
// more than the forward declaration below, so nothing here pulls it in.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

struct lua_State;

namespace rv_3dmppc
{

// The lua machine. OPTIONAL, unlike every other controller: a disc that
// declared no [budget.pccl] (rv_pccl_conf.hpp: script_memory_size == 0) gets
// no VM at all, and every contract call below then fails cleanly instead of
// touching a machine that was never asked for. Same shape as rv_pcca's
// --no-audio.
class rv_pccl
{
private:
    rv_pccl_conf conf_;
    rv_pccd &cd_; // BORROWED. Reads the entry asset out of the archive.

    lua_State *L_ = nullptr; // the whole machine; null means scripting is off

    int64_t budget_ = 0; // conf_.script_memory_size, cached for the allocator
    int64_t used_ = 0;   // bytes the allocator currently has outstanding

    // PATTERN: handle table, the same idea as rv_pccd's resource table — a
    // handle is an index into chunks_, and it is NEVER reused: LUA_NOREF
    // marks a slot script_free() emptied, so a stale handle finds nothing
    // rather than landing on somebody else's chunk.
    std::vector<int> chunks_;

    int64_t entry_ = -1; // memoised script_entry() handle; -1 = not raised yet

    // lua_Alloc for this machine: a realloc that refuses to push used_ past
    // budget_. `ud` is the rv_pccl the state was created with (lua_newstate).
    static void *rv_alloc(void *ud, void *ptr, size_t osize, size_t nsize);

    // Installed via lua_atpanic: by default an error raised outside every
    // pcall calls abort() and the console dies with nothing in the log.
    static int panic(lua_State *L);

    // Builds the console<->script vocabulary: opens ffi, feeds it
    // rv_pdk_cdef, and turns rv_pdk_consts into the global `pdk` table (see
    // rv_pccl.cpp for the whole recipe and why it is not inlined in the
    // constructor). Returns false on ANY failure - a bad cdef, a symbol the
    // build never exported - and touches nothing that survives that: the
    // constructor closes L_ down on a false return, same as a failed
    // lua_newstate.
    bool bootstrap_pdk();

public:
    rv_pccl(const rv_pccl_conf &conf, rv_pccd &cd);
    ~rv_pccl();

    rv_pccl(const rv_pccl &) = delete;
    rv_pccl &operator=(const rv_pccl &) = delete;

    // True when the machine is in the state it was asked to be in: scripting
    // off is TRUE (it was never asked for), scripting on and no VM is the
    // only FALSE — the distinction rv_pcca::valid() draws for --no-audio.
    bool valid() const;

    // True only when a lua_State actually exists. NOT the same question as
    // valid(): scripting off is TRUE for valid() (that state was asked for)
    // but FALSE here — this is what cl() asks to decide whether the disc
    // gets a handle to this machine at all.
    bool scripting() const
    {
        return L_ != nullptr;
    }

    int64_t script_load(const void *bytecode, int64_t size, const char *name);
    int64_t script_free(int64_t handle);
    int64_t script_entry();

    int64_t stack_push_nil();
    int64_t stack_push_boolean(bool value);
    int64_t stack_push_integer(int64_t value);
    int64_t stack_push_number(double value);
    int64_t stack_push_string(const char *text, int64_t length);
    int64_t stack_push_pointer(void *p);
    int64_t stack_drop(int64_t count);
    int64_t stack_count();

    int64_t value_type(int64_t index);
    int64_t value_boolean(int64_t index, bool *out);
    int64_t value_integer(int64_t index, int64_t *out);
    int64_t value_number(int64_t index, double *out);
    int64_t value_string(int64_t index, char *baddr, int64_t baddr_size);

    int64_t script_call(int64_t handle, const char *fname, int64_t argc, int64_t retc);
};

} // namespace rv_3dmppc
