// The rv_cl contract made abstract: script loading, the stack protocol, and
// the error vocabulary. Exactly what "running a script" means underneath is
// a choice made by whichever concrete class the console picks - a real lua
// machine (rv_pccl_luajit) or a no-op that answers RV_ERR_INVAL to every
// call (rv_pccl_null).
#pragma once

#include <cstdint>
#include <string>

namespace rv_3dmppc
{

// What the development channel reports about the script machine. One struct
// rather than eight accessors: every field is read at the same moment, by the
// same command, and a `status` that had to make eight virtual calls would be
// eight chances for the picture to be inconsistent.
struct rv_pccl_status {
    int64_t revision = 0;   // successful entry reloads since boot
    uint64_t hash = 0;      // FNV-1a of the bytes the entry chunk is running now
    int64_t used = 0;       // bytes the script heap holds
    int64_t budget = 0;     // the ceiling it holds them under
    int64_t slots = 0;      // handle-table size; must not grow with reloads
    int64_t error_seq = 0;  // bumped on every failed hook call, never reset
    bool reloadable = false;// the entry was raised AND it has attach()
    std::string error;      // the last hook failure, empty when there was none
};

// One value read out of the persistent state table. `type` carries an
// RV_CL_TYPE_* code, or -1 for "no such key" - a plain lua table stores no nil,
// so absence and nil are the same fact and get the same answer.
struct rv_pccl_value {
    int64_t type = -1;
    bool boolean = false;
    double number = 0.0;
    std::string bytes; // a string value, raw: it may hold NUL and invalid UTF-8
};

// Why a reload did not happen, in the two shapes the answer needs: a stable
// token the client branches on, and a sentence a person reads.
//
// `effects_possible` is a CONSERVATIVE statement. Once the candidate's body or
// its attach() has run, it may have written into the state table or called
// hardware, and nothing can take that back: a voice already fed the mixer, a
// video address the old code has never heard of is already allocated. The
// contract is therefore "atomic in code, not in effects", and this flag is how
// the console says so out loud instead of implying a rollback it cannot do.
struct rv_pccl_reload_report {
    const char *phase = "";
    bool effects_possible = false;
    std::string message;
};

class rv_pccl
{
public:
    virtual ~rv_pccl() = default;

    rv_pccl(const rv_pccl &) = delete;
    rv_pccl &operator=(const rv_pccl &) = delete;

    virtual int64_t script_load(const void *bytecode, int64_t size, const char *name) = 0;
    virtual int64_t script_free(int64_t handle) = 0;
    virtual int64_t script_entry() = 0;

    virtual int64_t stack_push_nil() = 0;
    virtual int64_t stack_push_boolean(bool value) = 0;
    virtual int64_t stack_push_integer(int64_t value) = 0;
    virtual int64_t stack_push_number(double value) = 0;
    virtual int64_t stack_push_string(const char *text, int64_t length) = 0;
    virtual int64_t stack_push_pointer(void *p) = 0;
    virtual int64_t stack_drop(int64_t count) = 0;
    virtual int64_t stack_count() = 0;

    virtual int64_t value_type(int64_t index) = 0;
    virtual int64_t value_boolean(int64_t index, bool *out) = 0;
    virtual int64_t value_integer(int64_t index, int64_t *out) = 0;
    virtual int64_t value_number(int64_t index, double *out) = 0;
    virtual int64_t value_string(int64_t index, char *baddr, int64_t baddr_size) = 0;

    virtual int64_t script_call(int64_t handle, const char *fname, int64_t argc, int64_t retc) = 0;

    // --- the development runtime: console-side only ------------------------
    //
    // None of the five below is reachable through the extern "C" block, and
    // that is the point. A disc cannot ask for a reload, and neither can a
    // script: the pdk metatable resolves any rv_* symbol through ffi.C, so a
    // contract function here would hand a chunk the means to replace itself
    // half way through its own frame. The editor asks; the disc is not told.

    // Replace the entry chunk's code, keeping its handle. Every check runs
    // BEFORE the old code is let go - compile, run the body, demand a table,
    // demand attach(), and run attach() against the live state - so a refusal
    // always leaves the running code untouched. On success the handle the disc
    // holds now names the new chunk and the disc never learns anything changed.
    virtual int64_t script_reload_entry(const void *bytecode, int64_t size, const char *name,
        rv_pccl_reload_report &report) = 0;

    // The same, with the bytes taken from the drive: the entry asset is read
    // again through rv_cd. Only ever useful when the mounted medium can change
    // under the console, which the CALLER decides (rv_pconsole_params::
    // medium_live) - in an archive the bytes are the same bytes.
    virtual int64_t script_reload_entry_from_drive(rv_pccl_reload_report &report) = 0;

    // Read one top-level field of the persistent state table. RAW: no metatable
    // is consulted, so inspecting state can never run script code. That is not
    // a detail - a dev channel that evaluates is a dev channel that can be
    // asked to do anything.
    // Tell the entry chunk that one asset of the mounted medium has changed on
    // disk, so it can re-read and re-upload it.
    //
    // The console does NOT refresh the asset itself, and cannot: a texture
    // lives at an address in virtual VRAM that the game's own code chose, and
    // nothing outside that code knows which address, how big it was, or whether
    // anything still points at it. So the division is fixed - the console
    // notifies, the game re-uploads - and the same "atomic in code, not in
    // effects" rule applies: once the hook has started writing into VRAM, a
    // failure part way through cannot be taken back.
    virtual int64_t script_asset_changed(const char *name, rv_pccl_reload_report &report) = 0;

    virtual int64_t state_get(const char *key, rv_pccl_value &out) = 0;

    // Full collection, then the resulting heap size. Exists for one reason: a
    // leak check needs a number that is not mostly garbage.
    virtual int64_t state_collect(int64_t *used_out) = 0;

    virtual void script_status(rv_pccl_status &out) const = 0;

    // Does the machine this controller owns actually exist? Console-side
    // only - not reached through the extern "C" block.
    virtual bool valid() const = 0;

protected:
    rv_pccl() = default;
};

} // namespace rv_3dmppc
