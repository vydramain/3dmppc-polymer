// The development runtime: raising a candidate under the instruction ceiling,
// the gate hooks that can refuse it, and the two reload routes.
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

// The incompatible-state gate. The console has no schema for the state table
// and cannot tell that `player.hp` used to be a number - only the new code
// knows what it expects, so only the new code can say no.
int64_t rv_pccl_luajit::attach_(int ref, rv_pccl_reload_report &report)
{
    static constexpr gate_phases phases{ "no_attach", "attach", "attach_refused",
        "attach_contract" };
    return call_gate_(ref, "attach", nullptr, phases, report);
}

// The asset gate. Same protocol, different question: not "can you take this
// state" but "could you take this refreshed asset".
int64_t rv_pccl_luajit::script_asset_changed(const char *name, rv_pccl_reload_report &report)
{
    static constexpr gate_phases phases{ "no_asset_hook", "asset", "asset_refused",
        "asset_contract" };
    if (name == nullptr) {
        report.phase = "bad_request";
        report.message = "no asset name";
        return RV_ERR_INVAL;
    }
    if (entry_ < 0) {
        report.phase = "no_entry";
        report.message = "the entry chunk has not been raised yet";
        return RV_ERR_INVAL;
    }
    if (call_depth_ > 0) {
        report.phase = "in_call";
        report.message = "a script call is in flight";
        return RV_ERR_BUSY;
    }
    const int ref = chunks_[static_cast<std::size_t>(entry_)].ref;
    if (!has_hook_(ref, "asset_changed")) {
        // Answered before the hook is called rather than after, so the
        // "effects" flag can honestly say nothing ran: unlike a reload, there
        // is no candidate body here to have executed first.
        report.phase = phases.missing;
        report.effects_possible = false;
        report.message = "the entry chunk has no asset_changed() function";
        return RV_ERR_INVAL;
    }
    return call_gate_(ref, "asset_changed", name, phases, report);
}

int64_t rv_pccl_luajit::reload_entry_bytes_(const void *bytecode, int64_t size, const char *name,
    rv_pccl_reload_report &report)
{
    if (entry_ < 0) {
        report.phase = "no_entry";
        report.message = "the entry chunk has not been raised yet";
        return RV_ERR_INVAL;
    }
    if (call_depth_ > 0) {
        // Not a failure of the candidate: the same request one frame boundary
        // later will be fine, which is exactly what RV_ERR_BUSY means.
        report.phase = "in_call";
        report.message = "a script call is in flight";
        return RV_ERR_BUSY;
    }
    if (!entry_attach_) {
        report.phase = "not_reloadable";
        report.message = "the entry chunk has no attach(); its state could not be carried across";
        return RV_ERR_INVAL;
    }

    int candidate = 0;
    const int64_t raised = raise_(bytecode, size, name, candidate, report);
    if (raised < 0) {
        return raised;
    }
    // attach() runs on the CANDIDATE, before the swap. That order is the whole
    // guarantee: everything that can refuse has refused by the time the old
    // reference is let go, so there is no state in which the code has been
    // replaced but the replacement was never accepted - and therefore nothing
    // to roll back.
    const int64_t attached = attach_(candidate, report);
    if (attached < 0) {
        luaL_unref(L_, LUA_REGISTRYINDEX, candidate);
        return attached;
    }

    // --- the commit. Nothing below is allowed to fail. ---
    chunk_slot &slot = chunks_[static_cast<std::size_t>(entry_)];
    const int previous = slot.ref;
    slot.ref = candidate;
    if (name != nullptr) {
        slot.name = name;
    }
    luaL_unref(L_, LUA_REGISTRYINDEX, previous);
    ++revision_;
    entry_hash_ = rv_pccl_fnv1a(bytecode, size);
    report.phase = "ok";
    report.effects_possible = false;
    report.message.clear();
    RV_LOG_INFO("pccl", "entry chunk replaced from '{}' (revision {}, {} byte(s), hash {:016x})",
        rv_pdklib::rv_log_escape(slot.name.c_str()), revision_, size, entry_hash_);
    return RV_OK;
}

int64_t rv_pccl_luajit::script_reload_entry(const void *bytecode, int64_t size, const char *name,
    rv_pccl_reload_report &report)
{
    return reload_entry_bytes_(bytecode, size, name, report);
}

// The bytes come off the drive instead of the wire. Whether that is meaningful
// is the CALLER's judgement (rv_pconsole_params::medium_live): in an archive the
// entry cannot have changed, and re-reading it would answer ok while changing
// nothing.
int64_t rv_pccl_luajit::script_reload_entry_from_drive(rv_pccl_reload_report &report)
{
    if (entry_ < 0) {
        report.phase = "no_entry";
        report.message = "the entry chunk has not been raised yet";
        return RV_ERR_INVAL;
    }

    const int64_t handle = cd_.asset_open(conf_.script_entry.c_str());
    if (handle < 0) {
        report.phase = "drive";
        report.message = "the drive has no entry asset by that name any more";
        return handle;
    }
    const int64_t size = cd_.asset_size(handle);
    if (size <= 0) {
        report.phase = "drive";
        report.message = "the entry asset is empty or cannot be measured";
        return size < 0 ? size : RV_ERR_INVAL;
    }
    // Read into a bounded buffer of our own. The file may be mid-save by an
    // editor, and a short or oversized read must cost a refusal, never a guess.
    std::vector<char> bytes(static_cast<std::size_t>(size));
    const int64_t read = cd_.asset_read(handle, bytes.data(), size);
    if (read < 0) {
        report.phase = "drive";
        report.message = "the entry asset could not be read";
        return read;
    }
    return reload_entry_bytes_(bytes.data(), read, conf_.script_entry.c_str(), report);
}

} // namespace rv_3dmppc
