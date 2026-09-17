// The development channel's command dispatcher: one answer per request, in
// request order, on stdout.
//
// Separate from the frame loop on purpose. The loop decides WHEN a command may
// run - only on a frame boundary, where no script call is in flight and the lua
// stack is at its base - and this file decides WHAT each one does. Mixing the
// two put the timing rule and twenty command handlers in one place, and the
// timing rule is the part that has to stay readable.
//
// The shape is deliberately not JSON: every value that could carry a space, a
// newline or a NUL travels as hex, and once that is true there is nothing left
// to escape - so the protocol needs no serialiser, and the console needs no
// dependency to speak it.
#include "rv_pconsole/rv_pconsole.hpp"

#include <format>
#include <string>
#include <string_view>

#include "pdk/cl/rv_cl.h"
#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

// --- the development runtime -------------------------------------------------
//
// One answer per request, in request order, on stdout. The shape is
// deliberately not JSON: every value that could carry a space, a newline or a
// NUL travels as hex, and once that is true there is nothing left to escape -
// so the protocol needs no serialiser, and the console needs no dependency to
// speak it.

namespace
{

std::string rv_pcdev_err(int64_t id, const char *token, int64_t rc, bool effects,
    std::string_view message)
{
    return std::format("{} err error={} rv_err={} effects={} msg={}", id, token, rc, effects ? 1 : 0,
        rv_3dmppc::rv_pcdev_hex(message));
}

} // namespace

void rv_3dmppc::rv_pconsole::dev_service()
{
    if (!dev_->connected()) {
        // Said once. The run carries on, and the pause state is deliberately
        // NOT touched: resuming here would restart a game the developer
        // stopped on purpose, at the moment they are least able to see why.
        if (!dev_close_logged_) {
            dev_close_logged_ = true;
            RV_LOG_WARN("pconsole",
                "development channel closed ({}); the run continues and the pause state is left as it is",
                dev_->closed_reason());
        }
        return;
    }

    rv_pcdevreq req;
    for (int64_t taken = 0; taken < RV_PCDEVCHAN_REQS_PER_TICK; ++taken) {
        if (!dev_->next_request(req)) {
            break;
        }
        dev_dispatch(req);
        // A step SUSPENDS the queue. Draining on would collapse three step
        // requests into one frame, and would answer a status for a frame that
        // has not happened yet.
        if (step_reply_id_ >= 0 || quit_by_command_) {
            break;
        }
    }
}

void rv_3dmppc::rv_pconsole::dev_dispatch(const rv_pcdevreq &req)
{
    const std::string_view verb = req.verb();

    if (verb == "status") {
        dev_status(req.id);
        return;
    }
    if (verb == "pause") {
        // Answered only now, which is after the decision and before the frame
        // that will not run: "ok" means no further frame happens until resume
        // or step.
        paused_ = true;
        dev_->reply(std::format("{} ok mode=paused frame={}", req.id, frames_));
        return;
    }
    if (verb == "resume") {
        paused_ = false;
        dev_->reply(std::format("{} ok mode=running frame={}", req.id, frames_));
        return;
    }
    if (verb == "step") {
        // No answer here: the frame has not run. disc_run answers once it has.
        paused_ = true;
        step_reply_id_ = req.id;
        return;
    }
    if (verb == "quit") {
        // The ORDINARY way out. Answering first and breaking the loop after
        // means the run leaves by the same path a closed window takes, so
        // disc_shutdown, the loader teardown and the frame dump all still
        // happen - there is no second shutdown path to keep in step.
        quit_by_command_ = true;
        dev_->reply(std::format("{} ok mode=stopped frame={}", req.id, frames_));
        return;
    }
    if (verb == "gc") {
        int64_t used = 0;
        const int64_t rc = cl_->state_collect(&used);
        if (rc < 0) {
            dev_->reply(rv_pcdev_err(req.id, "no_machine", rc, false, "this disc declared no lua machine"));
            return;
        }
        rv_pccl_status script;
        cl_->script_status(script);
        dev_->reply(std::format("{} ok lua_used={} lua_budget={} chunks={}", req.id, used,
            script.budget, script.slots));
        return;
    }
    if (verb == "reload") {
        dev_reload(req);
        return;
    }
    if (verb == "get") {
        dev_get(req);
        return;
    }
    if (verb == "asset") {
        dev_asset(req);
        return;
    }

    dev_->reply(rv_pcdev_err(req.id, "protocol", RV_ERR_INVAL, false,
        "unknown request; this console speaks status pause resume step reload asset get gc quit"));
}

void rv_3dmppc::rv_pconsole::dev_status(int64_t id)
{
    rv_pccl_status script;
    cl_->script_status(script);

    // `disc` identifies what is LOADED, not what is on disk: the manifest and
    // the budget were consumed at construction and this machine is built from
    // them, so a rebuild on disk changes nothing here. Comparing the two is the
    // client's job, and a difference means restart the process - native code
    // cannot be swapped under a live disc.
    const std::string disc_id = loader_ != nullptr ? loader_->info().disc_id : std::string("builtin");
    // The checksum of the disc.so this process actually mapped. `entry_hash`
    // next to it is the LUA half - the bytes the entry chunk is running. Two
    // hashes because there are two kinds of code, and only one of them can be
    // replaced without a restart; one number answering both questions would
    // answer neither.
    const std::string code_hash =
        loader_ != nullptr && !loader_->code_hash().empty() ? loader_->code_hash() : std::string("none");

    dev_->reply(std::format(
        "{} ok protocol=1 frame={} mode={} medium={} disc={} disc_hash={} pdk={}.{} "
        "entry_reloadable={} entry_revision={} entry_hash={:016x} lua_used={} lua_budget={} "
        "chunks={} error_seq={} script_error={}",
        id, frames_, paused_ ? "paused" : "running", params_.medium_live ? "live" : "fixed",
        rv_pcdev_hex(disc_id), code_hash, RV_MPPC_VER_MAJOR, RV_MPPC_VER_MINOR, script.reloadable ? 1 : 0,
        script.revision, script.hash, script.used, script.budget, script.slots, script.error_seq,
        rv_pcdev_hex(script.error)));
}

void rv_3dmppc::rv_pconsole::dev_reload(const rv_pcdevreq &req)
{
    // `entry` is a literal selector, not a name: version 1 replaces the entry
    // chunk and nothing else. A chunk the disc raised itself has a handle only
    // the disc knows, and inventing a lookup for it would be answering a
    // question nobody has asked yet.
    if (req.arg(0) != "entry") {
        dev_->reply(rv_pcdev_err(req.id, "unsupported_target", RV_ERR_INVAL, false,
            "this protocol version reloads the entry chunk only"));
        return;
    }

    rv_pccl_reload_report report;
    int64_t rc = 0;
    if (req.has_payload) {
        // Compiled under the entry's own asset name, because that name is what
        // lua puts in front of every error message the chunk produces.
        rc = cl_->script_reload_entry(req.payload.data(), static_cast<int64_t>(req.payload.size()),
            script_entry_.empty() ? "reload" : script_entry_.c_str(), report);
    } else {
        if (!params_.medium_live) {
            // Re-reading an archive entry would answer ok and change nothing:
            // the bytes in a zip cannot have moved. Refusing says so instead of
            // costing a frame to prove it.
            dev_->reply(rv_pcdev_err(req.id, "unsupported_medium", RV_ERR_INVAL, false,
                "the mounted medium cannot change; send the bytes, or boot an unpacked directory"));
            return;
        }
        rc = cl_->script_reload_entry_from_drive(report);
    }

    if (rc < 0) {
        dev_->reply(rv_pcdev_err(req.id, report.phase, rc, report.effects_possible, report.message));
        return;
    }

    rv_pccl_status script;
    cl_->script_status(script);
    dev_->reply(std::format("{} ok entry_revision={} entry_hash={:016x} lua_used={}", req.id,
        script.revision, script.hash, script.used));
}

void rv_3dmppc::rv_pconsole::dev_asset(const rv_pcdevreq &req)
{
    const std::string_view name = req.arg(0);
    if (name.empty()) {
        dev_->reply(rv_pcdev_err(req.id, "protocol", RV_ERR_INVAL, false,
            "asset needs the name of an entry on the mounted medium"));
        return;
    }
    if (!params_.medium_live) {
        // An archive entry cannot have changed, so there is nothing to refresh
        // and telling the game otherwise would have it re-upload the same bytes
        // and report success.
        dev_->reply(rv_pcdev_err(req.id, "unsupported_medium", RV_ERR_INVAL, false,
            "the mounted medium cannot change; boot an unpacked directory"));
        return;
    }

    // Resolved here so the answer can tell "there is no such entry" from "the
    // game refused it". The drive's name table is fixed at boot, which is also
    // why an ADDED asset needs a restart while a CHANGED one does not.
    const std::string key(name);
    if (cd_->asset_open(key.c_str()) < 0) {
        dev_->reply(rv_pcdev_err(req.id, "no_asset", RV_ERR_NOENT, false,
            "the mounted medium has no entry by that name"));
        return;
    }

    // The drive refreshes the resident texture: residency id is stable, but
    // addresses change. The game picks it up by querying for the address each draw.
    const int64_t rc = cd_->texture_reload(key.c_str());
    if (rc < 0) {
        dev_->reply(rv_pcdev_err(req.id, "asset", rc, false, "the drive could not reload that asset"));
        return;
    }
    // The new size, read back through the ordinary contract rather than a
    // console-only accessor: an acquire of a name already resident bumps the
    // refcount and hands back the same id, so this costs no upload and the
    // release below puts the count back exactly where it was. The editor
    // needs the numbers because a RESIZED texture is the one case its own
    // layout has to follow, and nothing else in the protocol carries them.
    int64_t width = 0;
    int64_t height = 0;
    const int64_t res = cd_->texture_acquire(key.c_str());
    if (res >= 0) {
        width = cd_->texture_width(res);
        height = cd_->texture_height(res);
        cd_->texture_release(res);
    }
    dev_->reply(std::format("{} ok asset={} width={} height={}", req.id, rv_pcdev_hex(key), width,
        height));
}

void rv_3dmppc::rv_pconsole::dev_get(const rv_pcdevreq &req)
{
    const std::string_view key = req.arg(0);
    if (key.empty()) {
        dev_->reply(rv_pcdev_err(req.id, "protocol", RV_ERR_INVAL, false, "get needs a key"));
        return;
    }

    rv_pccl_value value;
    const int64_t rc = cl_->state_get(std::string(key).c_str(), value);
    if (rc == RV_ERR_NOMEM) {
        // Looking a key up interns it, and interning allocates: on a machine
        // that has run its script heap out, the read cannot be performed at
        // all. Answered, not fatal - `gc` is the next thing to try, and the
        // client has to be able to reach it.
        dev_->reply(rv_pcdev_err(req.id, "nomem", rc, false,
            "the script heap is exhausted; the key could not be interned. try gc"));
        return;
    }
    if (rc < 0) {
        dev_->reply(rv_pcdev_err(req.id, "no_machine", rc, false, "this disc declared no lua machine"));
        return;
    }

    switch (value.type) {
    case RV_CL_TYPE_BOOLEAN:
        dev_->reply(std::format("{} ok found=1 type=boolean value={}", req.id, value.boolean ? 1 : 0));
        return;
    case RV_CL_TYPE_NUMBER:
        dev_->reply(std::format("{} ok found=1 type=number value={}", req.id, value.number));
        return;
    case RV_CL_TYPE_STRING: {
        // Hex doubles the byte count, and the answer queue has its own
        // ceiling; decided here, from the value's length, because building
        // the line first and recovering after would already have queued too
        // much.
        // The fixed part of the line counts too: a value at exactly half the
        // ceiling would pass a hex-only check and then overflow the queue by
        // the length of this prefix, which is the same defect one step smaller.
        const std::string prefix = std::format("{} ok found=1 type=string value=", req.id);
        const int64_t line_size =
            static_cast<int64_t>(prefix.size()) + static_cast<int64_t>(value.bytes.size()) * 2 + 1;
        if (line_size > RV_PCDEVCHAN_OUT_MAX) {
            dev_->reply(rv_pcdev_err(req.id, "answer_size", RV_ERR_INVAL, false,
                std::format("value is {} bytes; its hex answer does not fit one reply (ceiling {} bytes)",
                    value.bytes.size(), RV_PCDEVCHAN_OUT_MAX)));
            return;
        }
        // Hex, not text: a stored string may hold a NUL or bytes that are not
        // valid UTF-8, and the protocol promises to hand back what is there.
        dev_->reply(std::format("{} ok found=1 type=string value={}", req.id,
            rv_pcdev_hex(value.bytes)));
        return;
    }
    case RV_CL_TYPE_TABLE:
        dev_->reply(std::format("{} ok found=1 type=table", req.id));
        return;
    case RV_CL_TYPE_FUNCTION:
        // Worth its own type name rather than "other": a function in state
        // keeps the old chunk's code alive and callable past a reload, which is
        // the one state-table mistake that looks like nothing at all.
        dev_->reply(std::format("{} ok found=1 type=function", req.id));
        return;
    case RV_CL_TYPE_OTHER:
        dev_->reply(std::format("{} ok found=1 type=other", req.id));
        return;
    default:
        // A lua table stores no nil, so "no such key" and "nil" are one fact.
        dev_->reply(std::format("{} ok found=0 type=nil", req.id));
        return;
    }
}
