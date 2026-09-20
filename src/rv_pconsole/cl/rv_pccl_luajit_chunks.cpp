// Chunks: raising one, letting one go, the entry chunk, and calling into one.
//
// lua.hpp is confined to src/rv_pconsole/cl/rv_pccl_luajit* - see
// rv_pccl_luajit_detail.hpp.
#include "rv_pconsole/cl/rv_pccl_luajit.hpp"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include "lua.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pconsole/cl/rv_pccl_luajit_detail.hpp"

namespace rv_3dmppc
{
int64_t rv_pccl_luajit::script_load(const void *bytecode, int64_t size, const char *name)
{
    int ref = 0;
    rv_pccl_reload_report report;
    // Not the entry: script_entry() below raises the entry chunk itself, with
    // its own call to raise_, so that the entry's environment is wired in
    // before this generic path ever sees it. Anything reaching script_load
    // gets the real globals table, same as a required module.
    const int64_t raised = raise_(bytecode, size, name, ref, report, /*is_entry=*/false);
    if (raised < 0) {
        return raised;
    }
    // The ONLY place a handle is minted. A reload deliberately does not come
    // through here: it reuses the slot it already has, so a session of a
    // thousand keystrokes does not leave a thousand dead slots behind.
    chunks_.push_back(chunk_slot{ ref, name != nullptr ? name : "" });
    return static_cast<int64_t>(chunks_.size() - 1);
}
int64_t rv_pccl_luajit::script_free(int64_t handle)
{
    if (handle < 0 || handle >= static_cast<int64_t>(chunks_.size())) {
        return RV_ERR_INVAL;
    }
    // A chunk must not be freed while its own code is on the stack. The pdk
    // metatable resolves any rv_* symbol through ffi.C, so a script handed a
    // cl pointer can reach rv_cl_script_free and aim it at itself - this is
    // where that ends, with a code that says "try again later" rather than an
    // interpreter running from a collected prototype.
    if (call_depth_ > 0) {
        RV_LOG_ERR("pccl", "script_free({}) refused: a script call is in flight", handle);
        return RV_ERR_BUSY;
    }
    int &ref = chunks_[static_cast<size_t>(handle)].ref;
    if (ref == LUA_NOREF) {
        return RV_ERR_INVAL; // already freed; the slot is never reused
    }
    luaL_unref(L_, LUA_REGISTRYINDEX, ref);
    ref = LUA_NOREF;
    return RV_OK;
}
int64_t rv_pccl_luajit::script_entry()
{
    if (entry_ >= 0) {
        return entry_; // raised once; later calls answer from memo
    }
    // The console parsed the manifest, so it - not the disc - knows the name.
    const int64_t handle = cd_.asset_open(conf_.script_entry.c_str());
    if (handle < 0) {
        return handle;
    }
    const int64_t size = cd_.asset_size(handle);
    if (size <= 0) {
        return size < 0 ? size : RV_ERR_INVAL; // an entry needs bytes
    }
    std::vector<char> bytes(static_cast<size_t>(size));
    const int64_t read = cd_.asset_read(handle, bytes.data(), size);
    if (read < 0) {
        return read;
    }
    // Raised directly, not through script_load(): only the entry gets
    // is_entry=true, which is what wires `state` into its environment before
    // its body ever runs (see raise_).
    int ref = 0;
    rv_pccl_reload_report raise_report;
    const int64_t raised = raise_(bytes.data(), read, conf_.script_entry.c_str(), ref, raise_report,
        /*is_entry=*/true);
    if (raised < 0) {
        return raised;
    }
    chunks_.push_back(chunk_slot{ ref, conf_.script_entry });
    entry_ = static_cast<int64_t>(chunks_.size() - 1);
    entry_hash_ = rv_pccl_fnv1a(bytes.data(), read);

    // No state-shape action here any more: the chunk body has only just run
    // (print("Hello from example lua!") and nothing else, by convention), and
    // `state` is still whatever it was before boot - empty. The shape is
    // captured later, from script_call, the moment this entry's own
    // disc_initialize hook has actually populated it.
    return entry_;
}
// The caller already pushed argc arguments; the function lands UNDERNEATH them.
int64_t rv_pccl_luajit::script_call(int64_t handle, const char *fname, int64_t argc, int64_t retc)
{
    if (fname == nullptr || argc < 0 || retc < 0 || argc > lua_gettop(L_)) {
        return RV_ERR_INVAL;
    }
    if (handle < 0 || handle >= static_cast<int64_t>(chunks_.size())) {
        return RV_ERR_INVAL;
    }
    const int ref = chunks_[static_cast<size_t>(handle)].ref;
    if (ref == LUA_NOREF) {
        return RV_ERR_INVAL; // handle names a freed chunk
    }
    // Stack height on entry, args included. Used only in builds without
    // NDEBUG, where the assert()s below prove the stack ends at top-argc after
    // a failure and at top-argc+retc after a success. With NDEBUG they expand
    // to nothing and top is never read, so -Wall -Wextra would flag it: hence
    // [[maybe_unused]].
    [[maybe_unused]] const int top = lua_gettop(L_);
    lua_rawgeti(L_, LUA_REGISTRYINDEX, ref); // [args..., T]
    lua_getfield(L_, -1, fname);             // [args..., T, fn?]
    const bool callable = lua_isfunction(L_, -1);
    lua_remove(L_, -2); // [args..., fn?]
    if (!callable) {    // args AND the non-function value must both leave the stack
        lua_pop(L_, static_cast<int>(argc) + 1);
        RV_LOG_ERR("pccl", "script_call: '{}' is not a function", fname);
        ++error_seq_;
        error_text_ = std::string(fname) + ": not a function";
        assert(lua_gettop(L_) == top - static_cast<int>(argc));
        return RV_ERR_INVAL;
    }
    lua_insert(L_, -static_cast<int>(argc) - 1); // [fn, args...]
    // From here until the pcall returns, script code owns the interpreter: a
    // reload or a free arriving in the meantime must refuse rather than pull
    // the prototype out from under it.
    int rc = 0;
    {
        rv_pccl_call_guard guard(call_depth_);
        // A hook that never returns would otherwise take the whole session with
        // it; hook_insn_ceiling_() is 0 in a player build, so this stays unguarded.
        const int ceiling = hook_insn_ceiling_();
        ceiling_hit_ = false;
        if (ceiling > 0) {
            const rv_pccl_insn_guard armed(L_, insn_hook, ceiling);
            rc = lua_pcall(L_, static_cast<int>(argc), static_cast<int>(retc), 0);
        } else {
            rc = lua_pcall(L_, static_cast<int>(argc), static_cast<int>(retc), 0);
        }
    }
    if (rc != 0) {
        // Contract: nothing survives a failed call - logged, then popped. The
        // message is also KEPT: the console watches error_seq_ to stop the run
        // in --dev and hands the text to `status`, so the developer reads the
        // lua error from the channel instead of hunting for it in stderr.
        const char *msg = lua_tostring(L_, -1);
        RV_LOG_ERR("pccl", "script_call('{}'): {}", fname, msg != nullptr ? msg : "(no message)");
        ++error_seq_;
        error_text_ = std::string(fname) + ": " + (msg != nullptr ? msg : "(no message)");
        lua_pop(L_, 1);
        assert(lua_gettop(L_) == top - static_cast<int>(argc));
        return RV_ERR_IO;
    }
    assert(lua_gettop(L_) == top - static_cast<int>(argc) + static_cast<int>(retc));

    // The entry chunk's disc_initialize, the first time it is ever called, is
    // the moment the script has finished setting `state` up (see
    // capture_state_shape_'s own comment) - rv_dscript.hpp calls this hook
    // exactly once per disc, always before any other one, so this is also the
    // only place a fresh boot's shape can be learned from. shape_captured_
    // makes the check one-shot rather than trusting that contract blindly.
    if (handle == entry_ && !shape_captured_ && std::strcmp(fname, "disc_initialize") == 0) {
        shape_captured_ = true;
        rv_pccl_reload_report shape_report;
        const int64_t captured = capture_state_shape_(/*initial=*/true, shape_report);
        if (captured < 0) {
            RV_LOG_ERR("pccl", "entry chunk's state was rejected right after disc_initialize ({}): {}",
                shape_report.phase, rv_pdklib::rv_log_escape(shape_report.message.c_str(), 256));
            ++error_seq_;
            error_text_ = std::string(fname) + ": " + shape_report.message;
            lua_pop(L_, static_cast<int>(retc)); // whatever disc_initialize returned; retc is 0 in practice
            assert(lua_gettop(L_) == top - static_cast<int>(argc));
            return captured;
        }
    }
    return RV_OK;
}

} // namespace rv_3dmppc
