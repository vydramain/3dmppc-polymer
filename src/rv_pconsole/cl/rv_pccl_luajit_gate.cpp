// Entering a chunk safely: raising bytecode into a table under the
// instruction ceiling, and calling one of its gate hooks under the same
// ceiling. Used by both the normal boot (rv_pccl_luajit_chunks.cpp) and the
// development reload path (rv_pccl_luajit_reload.cpp).
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

// Armed only for the duration of a reload. It raises, which unwinds into the
// pcall that raise_/attach_ set up, so a chunk that never finishes becomes an
// ordinary refusal instead of a console that has to be killed - and killing it
// would cost the developer the session they were working in.
void rv_pccl_luajit::insn_hook(lua_State *L, struct lua_Debug *)
{
    void *ud = nullptr;
    lua_getallocf(L, &ud);
    if (ud != nullptr) {
        static_cast<rv_pccl_luajit *>(ud)->ceiling_hit_ = true;
    }
    luaL_error(L, "instruction ceiling of %d reached; the chunk did not finish",
        RV_PCCL_RELOAD_INSN_CEILING);
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
    rv_pccl_reload_report &report)
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
    const rv_pccl_insn_guard ceiling(L_, insn_hook, RV_PCCL_RELOAD_INSN_CEILING);

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
    // loadbuffer only COMPILES; the module table is the RESULT of running the body.
    if (lua_pcall(L_, 0, 1, 0) != 0) {
        return fail("body", true, RV_ERR_IO);
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

// RAW lookup, deliberately: a metatable on the module table must not be able to
// decide what the console calls.
bool rv_pccl_luajit::has_hook_(int ref, const char *hook) const
{
    lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
    lua_pushstring(L_, hook);
    lua_rawget(L_, -2);
    const bool found = lua_isfunction(L_, -1);
    lua_pop(L_, 2);
    return found;
}

int64_t rv_pccl_luajit::call_gate_(int ref, const char *hook, const char *string_arg,
    const gate_phases &phases, rv_pccl_reload_report &report)
{
    [[maybe_unused]] const int top = lua_gettop(L_);
    oom_ = false;
    ceiling_hit_ = false;
    const rv_pccl_insn_guard ceiling(L_, insn_hook, RV_PCCL_RELOAD_INSN_CEILING);

    lua_rawgeti(L_, LUA_REGISTRYINDEX, ref); // [T]
    lua_pushstring(L_, hook);
    lua_rawget(L_, -2); // [T, fn?]
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 2);
        report.phase = phases.missing;
        // The body already ran to produce this table, so something of the
        // candidate has executed even though the hook it needed is absent.
        report.effects_possible = true;
        report.message = std::string("the chunk has no ") + hook + "() function";
        assert(lua_gettop(L_) == top);
        return RV_ERR_INVAL;
    }

    if (string_arg != nullptr) {
        lua_pushstring(L_, string_arg); // [T, fn, arg]
    } else {
        // The SAME table every chunk before it got, and every chunk after it
        // will: that lifetime is the whole of "code != state".
        lua_rawgeti(L_, LUA_REGISTRYINDEX, state_ref_); // [T, fn, state]
    }

    int rc = 0;
    {
        // Counts as script activity: a reload or a free arriving from inside
        // this hook must refuse, not swap code that is on the stack.
        const rv_pccl_call_guard guard(call_depth_);
        rc = lua_pcall(L_, 1, 2, 0); // [T, accepted, reason] or [T, error]
    }
    if (rc != 0) {
        const char *msg = lua_tostring(L_, -1);
        report.phase = phase_of(phases.raised);
        // It raised part way through, so whatever it had already written into
        // the state table, or asked of the hardware, is still written and still
        // asked. Nothing here can take that back.
        report.effects_possible = true;
        report.message = msg != nullptr ? msg : "(no message)";
        lua_pop(L_, 2);
        assert(lua_gettop(L_) == top);
        return oom_ ? RV_ERR_NOMEM : RV_ERR_IO;
    }

    // A boolean, and only a boolean. Accepting anything truthy would make
    // "forgot to return" read as "accepted", which is the one mistake this
    // check exists to catch.
    if (lua_type(L_, -2) != LUA_TBOOLEAN) {
        lua_pop(L_, 3);
        report.phase = phases.contract;
        report.effects_possible = true;
        report.message = std::string(hook) + "() must return true, or false and a reason";
        assert(lua_gettop(L_) == top);
        return RV_ERR_INVAL;
    }
    const bool accepted = lua_toboolean(L_, -2) != 0;
    std::string reason;
    if (lua_type(L_, -1) == LUA_TSTRING) {
        std::size_t len = 0;
        const char *text = lua_tolstring(L_, -1, &len);
        reason.assign(text, len);
    }
    lua_pop(L_, 3);
    assert(lua_gettop(L_) == top);

    if (!accepted) {
        report.phase = phases.refused;
        report.effects_possible = true;
        report.message = reason.empty() ? std::string(hook) + "() refused" : reason;
        return RV_ERR_INVAL;
    }

    report.phase = "ok";
    report.effects_possible = false;
    report.message.clear();
    return RV_OK;
}

// The incompatible-state gate. Two checks, deliberately not one:
// check_state_shape_ is STRUCTURAL and belongs to the console, because a
// disc author cannot get key-present/type-matches wrong in a way the console
// cannot see for itself. attach() is SEMANTIC and stays the chunk's call -
// the console has no schema for the state table and cannot tell that
// `player.hp` used to mean something different, only the new code knows what
// it expects, so only the new code can say no to that.
int64_t rv_pccl_luajit::attach_(int ref, rv_pccl_reload_report &report)
{
    static constexpr gate_phases phases{ "no_attach", "attach", "attach_refused",
        "attach_contract" };

    std::vector<state_shape_insert> inserted;
    const int64_t shaped = check_state_shape_(ref, inserted, report);
    if (shaped < 0) {
        return shaped; // report already named the field path; nothing was left mutated
    }

    const int64_t result = call_gate_(ref, "attach", nullptr, phases, report);
    // Whether attach() accepted or refused, the console's own pins are done
    // with; a refusal also asks for the keys themselves back out.
    finish_state_shape_(inserted, result >= 0);
    return result;
}

} // namespace rv_3dmppc
