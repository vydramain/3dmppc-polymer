// The development capability: replacing the entry chunk, reached only through
// the dev command channel, and the ceiling armed around every hook call in a
// development build - the two things that only exist when 3DMPPC_DEVTOOLS is on.
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
    // attach() runs on the CANDIDATE, before the patch. That order is the
    // whole guarantee: everything that can refuse has refused by the time the
    // running tables are touched, so there is no state in which the code has
    // been replaced but the replacement was never accepted - and therefore
    // nothing to roll back.
    const int64_t attached = attach_(candidate, report);
    if (attached < 0) {
        luaL_unref(L_, LUA_REGISTRYINDEX, candidate);
        return attached;
    }

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

} // namespace rv_3dmppc
