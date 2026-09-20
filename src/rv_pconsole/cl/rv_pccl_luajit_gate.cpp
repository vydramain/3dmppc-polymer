// Entering a chunk safely: raising bytecode into a table under the
// instruction ceiling, wiring the entry's environment in before its body
// runs, and settling the structural half of the incompatible-state question
// once it has. Used by both the normal boot (rv_pccl_luajit_chunks.cpp) and
// the development reload path (rv_pccl_luajit_reload.cpp).
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

#include "lua.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cl/rv_pccl_luajit_detail.hpp"

namespace rv_3dmppc
{

// Armed around a reload, and in a development build around every hook call. It
// raises, which unwinds into the pcall the caller set up, so a chunk that never
// finishes becomes an ordinary failure instead of a console that has to be
// killed - and killing it would cost the developer the session they were
// working in.
void rv_pccl_luajit::insn_hook(lua_State *L, struct lua_Debug *)
{
    void *ud = nullptr;
    lua_getallocf(L, &ud);
    if (ud != nullptr) {
        static_cast<rv_pccl_luajit *>(ud)->ceiling_hit_ = true;
    }
    luaL_error(L, "instruction ceiling of %d reached; the chunk did not finish",
        RV_PCCL_INSN_CEILING);
}

// Which phase name a failure deserves. The ceiling and the budget both surface
// as an ordinary lua error, so without these two flags they would be reported
// as whatever the caller was doing at the time.
const char *rv_pccl_luajit::phase_of(const char *phase) const
{
    if (ceiling_hit_) {
        return "insn_ceiling";
    }
    if (oom_) {
        return "nomem";
    }
    return phase;
}

int64_t rv_pccl_luajit::raise_(const void *bytecode, int64_t size, const char *name, int &ref_out,
    rv_pccl_reload_report &report, bool is_entry)
{
    if (bytecode == nullptr || size <= 0 || name == nullptr) {
        report.phase = "bad_request";
        report.effects_possible = false;
        report.message = "no bytes to raise";
        return RV_ERR_INVAL;
    }

    [[maybe_unused]] const int top = lua_gettop(L_);
    oom_ = false;
    ceiling_hit_ = false;
    // Covers compiling the source AND running its body. It does NOT cover a C
    // or FFI call the body makes, and it is not a wall-clock timeout: the
    // promise is bounded instructions, not bounded time.
    const rv_pccl_insn_guard ceiling(L_, insn_hook, RV_PCCL_INSN_CEILING);

    // Shared exit for a lua-level failure: name the phase, keep the message,
    // pop the error and prove the stack is where it was found.
    auto fail = [&](const char *phase, bool effects, int64_t code) {
        const char *msg = lua_tostring(L_, -1);
        report.phase = phase_of(phase);
        report.effects_possible = effects;
        report.message = msg != nullptr ? msg : "(no message)";
        RV_LOG_ERR("pccl", "raise('{}') failed at {}: {}", rv_pdklib::rv_log_escape(name),
            report.phase, rv_pdklib::rv_log_escape(report.message.c_str(), 256));
        lua_pop(L_, 1);
        assert(lua_gettop(L_) == top);
        return ceiling_hit_ || oom_ ? (oom_ ? RV_ERR_NOMEM : RV_ERR_IO) : code;
    };

    if (luaL_loadbuffer(L_, static_cast<const char *>(bytecode), static_cast<std::size_t>(size),
            name) != 0) {
        // Nothing of the candidate has run yet, so nothing of it can have left
        // a mark: this is the one failure that is provably clean.
        return fail("compile", false, RV_ERR_IO);
    }
    if (is_entry) {
        // Set on the closure BEFORE it runs, not after: a nested function
        // (M.disc_initialize, M.frame_update, ...) inherits whatever
        // environment the running function already has at the moment it is
        // defined, and every one of those is defined during the body run
        // that follows. Setting this any later would leave every hook this
        // chunk owns closed over the real globals table instead.
        const int close_idx = lua_gettop(L_);
        lua_rawgeti(L_, LUA_REGISTRYINDEX, entry_env_ref_);
        lua_setfenv(L_, close_idx);
    }
    // loadbuffer only COMPILES; the module table is the RESULT of running the body.
    //
    // Guarded, like every other place script code runs. A body is script code
    // with a pdk table in reach, so it can call rv_cl_script_free and aim it at
    // the chunk that is running RIGHT NOW: without the guard call_depth_ was 0,
    // the free was allowed, and the live entry went away. The candidate could
    // then be refused for any later reason and leave the console holding a
    // released entry - the old code gone, the new code never installed.
    {
        const rv_pccl_call_guard guard(call_depth_);
        if (lua_pcall(L_, 0, 1, 0) != 0) {
            return fail("body", true, RV_ERR_IO);
        }
    }
    if (!lua_istable(L_, -1)) {
        report.phase = "not_a_table";
        report.effects_possible = true; // the body ran before it returned the wrong thing
        report.message = "the chunk did not return a table";
        RV_LOG_ERR("pccl", "raise('{}'): {}", rv_pdklib::rv_log_escape(name), report.message);
        lua_pop(L_, 1);
        assert(lua_gettop(L_) == top);
        return RV_ERR_INVAL;
    }

    ref_out = luaL_ref(L_, LUA_REGISTRYINDEX); // pops the table
    assert(lua_gettop(L_) == top);
    report.phase = "ok";
    report.effects_possible = false;
    return RV_OK;
}

// The incompatible-state gate used to be two checks: a STRUCTURAL one the
// console could run for itself (check_state_shape_) and a SEMANTIC one
// only the new code could judge - attach(), which could refuse a
// structurally valid state on meaning alone even though nothing in this repo
// ever exercised that right. A PR review asked why attach lived in the
// console's own Lua machinery instead of the disc-facing half of the
// contract, and the owner's answer was to remove it rather than move it: the
// console now hands the entry chunk its state (see raise_'s `is_entry`
// wiring) instead of asking the chunk to accept delivery of it, and there is
// no replacement for the semantic refusal that went with it. What remains is
// the structural half: once check_state_shape_ has approved the whole tree,
// keep every default it planned.
int64_t rv_pccl_luajit::accept_state_shape_(int ref, rv_pccl_reload_report &report)
{
    std::vector<state_shape_insert> inserted;
    const int64_t shaped = check_state_shape_(ref, inserted, report);
    if (shaped < 0) {
        return shaped; // report already named the field path; nothing was left mutated
    }
    finish_state_shape_(inserted, true); // keep every default the walk inserted
    report.phase = "ok";
    report.effects_possible = false;
    report.message.clear();
    return RV_OK;
}

} // namespace rv_3dmppc
