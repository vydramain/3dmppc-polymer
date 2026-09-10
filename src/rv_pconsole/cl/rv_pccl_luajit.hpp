// The console's rv_cl implementation: the script machine. Counterpart of
// rv_pccv for rv_cv and rv_pcca for rv_ca — the contract is opaque C, the
// concrete class lives here.
//
// lua.hpp is the PDK boundary and is included by exactly ONE file in the
// whole project: rv_pccl_luajit.cpp. A pointer to an incomplete type needs
// nothing more than the forward declaration below, so nothing here pulls it in.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rv_pconsole/cd/rv_pccd.hpp"
#include "rv_pconsole/cl/rv_pccl.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

struct lua_State;

namespace rv_3dmppc
{

// The lua machine. Only ever built when the disc declared a lua machine
// (rv_pccl_conf.hpp: script_memory_size > 0) — a factory upstream guarantees
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

    // PATTERN: handle table, the same idea as rv_pccd's resource table — a
    // handle is an index into chunks_, and it is NEVER reused: LUA_NOREF
    // marks a slot script_free() emptied, so a stale handle finds nothing
    // rather than landing on somebody else's chunk.
    std::vector<int> chunks_;

    int64_t entry_ = -1; // memoised script_entry() handle; -1 = not raised yet

    // lua_Alloc for this machine: a realloc that refuses to push used_ past
    // budget_. `ud` is the rv_pccl_luajit the state was created with (lua_newstate).
    static void *rv_alloc(void *ud, void *ptr, size_t osize, size_t nsize);

    // Installed via lua_atpanic: by default an error raised outside every
    // pcall calls abort() and the console dies with nothing in the log.
    static int panic(lua_State *L);

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

    // A VM that failed to come up is the only FALSE: the console refuses to
    // boot on that, so no contract method below is ever reached with L_ null
    // — see the comment on that above valid()'s definition.
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
};

} // namespace rv_3dmppc
