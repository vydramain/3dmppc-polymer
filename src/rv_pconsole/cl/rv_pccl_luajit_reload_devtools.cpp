// The development capability: replacing the entry chunk or a require()d
// module's code, reached only through the dev command channel, and the
// ceiling armed around every hook call in a development build - the things
// that only exist when 3DMPPC_DEVTOOLS is on.
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include <cstddef>
#include <vector>

#include "lua.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cl/rv_pccl_luajit_detail.hpp"

namespace rv_3dmppc
{

// A development build guards every hook call the same way a reload guards a
// candidate's body: a hook that never returns must not take the session with it.
int rv_pccl_luajit::hook_insn_ceiling_()
{
    return RV_PCCL_INSN_CEILING;
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

    // Taken BEFORE the candidate is even raised: raise_ compiles AND RUNS the
    // candidate's chunk body to get the module table back, and that body
    // already has `state` in reach through entry_env_ref_ - the same wiring
    // every hook uses. A candidate that writes to state at its top level
    // (which the entry chunk's own comment on `state` asks scripts not to do)
    // would otherwise corrupt the table the OLD code is still running on,
    // with nothing left to put back if the candidate is then refused.
    int snapshot = 0;
    const int64_t snapped = snapshot_state_(snapshot, report);
    if (snapped < 0) {
        return snapped;
    }

    int candidate = 0;
    const int64_t raised = raise_(bytecode, size, name, candidate, report, /*is_entry=*/true);
    if (raised < 0) {
        luaL_unref(L_, LUA_REGISTRYINDEX, snapshot);
        return raised;
    }
    // The shape check runs on the LIVE state, after the candidate's body has
    // run - the reload's one and only equivalent of the moment disc_initialize
    // fixes the shape at boot, since a reload never calls disc_initialize
    // again. That order is still the whole guarantee: everything that can
    // refuse has refused by the time the running tables are patched, so there
    // is no state in which the code has been replaced but the replacement was
    // never accepted - and a refusal here also means the snapshot above is
    // about to give `state` itself back, not just the running code.
    const int64_t shaped = capture_state_shape_(/*initial=*/false, report);
    if (shaped < 0) {
        restore_state_(snapshot);
        luaL_unref(L_, LUA_REGISTRYINDEX, candidate);
        return shaped;
    }
    luaL_unref(L_, LUA_REGISTRYINDEX, snapshot);

    // --- the commit: the running tables take the new code in place. The patch
    // can still refuse on its size bounds before it touches anything, or run
    // out of heap part way through (nomem, effects=1). ---
    chunk_slot &slot = chunks_[static_cast<std::size_t>(entry_)];
    const int64_t patched = patch_in_place_(slot.ref, candidate, report);
    luaL_unref(L_, LUA_REGISTRYINDEX, candidate); // the candidate table itself is never kept
    if (patched < 0) {
        return patched; // report already filled by patch_in_place_; slot.ref still holds the old code
    }
    if (name != nullptr) {
        slot.name = name;
    }
    ++revision_;
    entry_hash_ = rv_pccl_fnv1a(bytecode, size);
    report.hash = entry_hash_;
    report.phase = "ok";
    report.effects_possible = false;
    report.message.clear();
    RV_LOG_INFO("pccl", "entry chunk updated in place from '{}' (revision {}, {} byte(s), hash {:016x})",
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


// Carries the module lookup across the protected call: interning `name` and
// taking a registry ref can both allocate.
struct module_lookup_args {
    int loaded_ref = 0;
    const char *name = nullptr;
    int ref_out = LUA_NOREF;
};

static int module_lookup_trampoline(lua_State *L)
{
    auto *args = static_cast<module_lookup_args *>(lua_touserdata(L, 1));
    lua_rawgeti(L, LUA_REGISTRYINDEX, args->loaded_ref);
    lua_pushstring(L, args->name);
    lua_rawget(L, -2);
    if (lua_istable(L, -1)) {
        args->ref_out = luaL_ref(L, LUA_REGISTRYINDEX); // pops the module table
    }
    return 0;
}

// Why module_asset_ refused a name, in the report's words.
static const char *module_asset_refusal(int code)
{
    return code == 1 ? "not a module name" : "the entry script is neither .lua nor .luac";
}

int64_t rv_pccl_luajit::module_find_(const char *name, char *asset, std::size_t cap, int &ref_out,
    rv_pccl_reload_report &report)
{
    if (call_depth_ > 0) {
        report.phase = "in_call";
        report.message = "a script call is in flight";
        return RV_ERR_BUSY;
    }
    const int asset_code = name != nullptr ? module_asset_(name, asset, cap) : 1;
    if (asset_code != 0) {
        report.phase = "no_module";
        report.message = module_asset_refusal(asset_code);
        return RV_ERR_INVAL;
    }

    // The running module is the table require() handed every caller: the one
    // whose identity the patch keeps. Looked up under protection, so a heap the
    // game has run out costs an answer, not the console.
    module_lookup_args lookup;
    lookup.loaded_ref = loaded_ref_;
    lookup.name = name;
    if (protected_call_(module_lookup_trampoline, &lookup) != 0) {
        lua_pop(L_, 1); // the error object
        report.phase = "nomem";
        report.message = "the script heap is exhausted; the module could not be looked up. try gc";
        return RV_ERR_NOMEM;
    }
    if (lookup.ref_out == LUA_NOREF) {
        report.phase = "no_module";
        report.message = "no module by that name has been required";
        return RV_ERR_NOENT;
    }
    ref_out = lookup.ref_out;
    return RV_OK;
}

int64_t rv_pccl_luajit::reload_module_bytes_(const char *name, const void *bytecode, int64_t size,
    rv_pccl_reload_report &report)
{
    char asset[256];
    int old_ref = LUA_NOREF;
    const int64_t found = module_find_(name, asset, sizeof asset, old_ref, report);
    if (found < 0) {
        return found;
    }

    // A module has no state of its own: compile, body and a returned table
    // are the whole of its checks - is_entry stays false, so it keeps the
    // real globals table it always had.
    int candidate = 0;
    const int64_t raised = raise_(bytecode, size, asset, candidate, report, /*is_entry=*/false);
    if (raised < 0) {
        luaL_unref(L_, LUA_REGISTRYINDEX, old_ref);
        return raised;
    }
    const int64_t patched = patch_in_place_(old_ref, candidate, report);
    luaL_unref(L_, LUA_REGISTRYINDEX, candidate);
    luaL_unref(L_, LUA_REGISTRYINDEX, old_ref);
    if (patched < 0) {
        return patched;
    }
    report.hash = rv_pccl_fnv1a(bytecode, size);
    report.phase = "ok";
    report.effects_possible = false;
    report.message.clear();
    RV_LOG_INFO("pccl", "module '{}' updated in place ({} byte(s), hash {:016x})", rv_pdklib::rv_log_escape(name),
        size, report.hash);
    return RV_OK;
}

int64_t rv_pccl_luajit::script_reload_module(const char *name, const void *bytecode, int64_t size,
    rv_pccl_reload_report &report)
{
    return reload_module_bytes_(name, bytecode, size, report);
}

// The bytes come off the drive, from the very asset require() would read -
// but only for a module something has required, so a name nobody required
// answers no_module whether or not the drive has a file by that name.
int64_t rv_pccl_luajit::script_reload_module_from_drive(const char *name, rv_pccl_reload_report &report)
{
    char asset[256];
    int old_ref = LUA_NOREF;
    const int64_t found = module_find_(name, asset, sizeof asset, old_ref, report);
    if (found < 0) {
        return found;
    }
    luaL_unref(L_, LUA_REGISTRYINDEX, old_ref);
    const int64_t handle = cd_.asset_open(asset);
    if (handle < 0) {
        report.phase = "drive";
        report.message = "the drive has no module asset by that name";
        return handle;
    }
    const int64_t size = cd_.asset_size(handle);
    if (size <= 0) {
        report.phase = "drive";
        report.message = "the module asset is empty or cannot be measured";
        return size < 0 ? size : RV_ERR_INVAL;
    }
    // Bounded and our own, as for the entry: the file may be mid-save.
    std::vector<char> bytes(static_cast<std::size_t>(size));
    const int64_t read = cd_.asset_read(handle, bytes.data(), size);
    if (read < 0) {
        report.phase = "drive";
        report.message = "the module asset could not be read";
        return read;
    }
    return reload_module_bytes_(name, bytes.data(), read, report);
}
} // namespace rv_3dmppc
