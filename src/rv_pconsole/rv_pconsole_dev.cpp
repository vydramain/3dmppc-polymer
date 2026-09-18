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
#include "rv_pconsole/cl/rv_pccl.hpp"
#include "rv_pconsole/platform/rv_pcdevhex.hpp"

// --- the development runtime -------------------------------------------------
//
// One answer per request, in request order, on stdout. The shape is
// deliberately not JSON: every value that could carry a space, a newline or a
// NUL travels as hex, and once that is true there is nothing left to escape -
// so the protocol needs no serialiser, and the console needs no dependency to
// speak it.

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

void rv_3dmppc::rv_pconsole::dev_after_frame()
{
    if (!dev_) {
        return;
    }

    // The step's answer, now that its frame is over. `step` means "one frame has
    // happened", so answering when the request arrived would be answering for
    // work not yet done.
    if (step_reply_id_ >= 0) {
        dev_->reply(
            std::format("{} ok completed=1 frame={} mode=paused", step_reply_id_, frames_ + 1));
        step_reply_id_ = -1;
    }

    // Did a game hook fail this frame? rv_pccl counts every failed call, so
    // comparing that count is how the console finds out without the disc having
    // to tell it and without a contract change. In a development run the machine
    // stops there: a frozen picture with no explanation is the worst possible
    // answer, and the developer needs the state as it was when it broke. id 0
    // marks a line nobody asked for.
    if (!cl_->valid()) {
        return;
    }
    rv_pccl_status script;
    cl_->script_status(script);
    if (script.error_seq != dev_error_seq_) {
        dev_error_seq_ = script.error_seq;
        paused_ = true;
        dev_->reply(std::format("0 event=script_error frame={} msg={}", frames_ + 1,
            rv_pcdev_hex_msg(script.error)));
    }
}

void rv_3dmppc::rv_pconsole::dev_note_pause()
{
    if (!dev_) {
        return;
    }
    // The client did not ask for this, so it arrives as an event: something
    // else moved the machine it is driving.
    dev_->reply(std::format("0 event=pause mode={} frame={}", paused_ ? "paused" : "running",
        frames_));
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
    if (verb == "keys") {
        dev_keys(req);
        return;
    }
    if (verb == "asset") {
        dev_asset(req);
        return;
    }

    dev_->reply(rv_pcdev_err(req.id, "protocol", RV_ERR_INVAL, false,
        "unknown request; this console speaks status pause resume step reload asset get keys gc quit"));
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
        rv_pcdev_hex_msg(script.error)));
}

void rv_3dmppc::rv_pconsole::dev_reload(const rv_pcdevreq &req)
{
    // Two selectors: `entry`, the chunk the manifest names, and `module <name>`,
    // a module require() loaded. Both update the running tables in place.
    if (req.arg(0) == "module") {
        dev_reload_module(req);
        return;
    }
    if (req.arg(0) != "entry") {
        dev_->reply(rv_pcdev_err(req.id, "unsupported_target", RV_ERR_INVAL, false,
            "reload takes `entry` or `module <name>`"));
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

void rv_3dmppc::rv_pconsole::dev_reload_module(const rv_pcdevreq &req)
{
    const std::string name(req.arg(1));
    if (name.empty()) {
        dev_->reply(rv_pcdev_err(req.id, "protocol", RV_ERR_INVAL, false, "reload module needs the module's name"));
        return;
    }

    rv_pccl_reload_report report;
    int64_t rc = 0;
    if (req.has_payload) {
        rc = cl_->script_reload_module(name.c_str(), req.payload.data(), static_cast<int64_t>(req.payload.size()),
            report);
    } else {
        // Same rule as the entry: an archive cannot have changed under the console.
        if (!params_.medium_live) {
            dev_->reply(rv_pcdev_err(req.id, "unsupported_medium", RV_ERR_INVAL, false,
                "the mounted medium cannot change; send the bytes, or boot an unpacked directory"));
            return;
        }
        rc = cl_->script_reload_module_from_drive(name.c_str(), report);
    }

    if (rc < 0) {
        dev_->reply(rv_pcdev_err(req.id, report.phase, rc, report.effects_possible, report.message));
        return;
    }

    rv_pccl_status script;
    cl_->script_status(script);
    dev_->reply(std::format("{} ok module={} hash={:016x} lua_used={}", req.id, rv_pcdev_hex(name), report.hash,
        script.used));
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

    // Resolved here so the answer can tell "no such entry" from "the drive
    // refused it"; the medium is asked at open time, so an entry added to a
    // live directory after boot is found, and a texture nobody holds
    // resident has nothing to refresh.
    const std::string key(name);
    if (cd_->asset_open(key.c_str()) < 0) {
        dev_->reply(rv_pcdev_err(req.id, "no_asset", RV_ERR_NOENT, false,
            "the mounted medium has no entry by that name"));
        return;
    }

    // The drive refreshes the resident texture: residency id is stable, but
    // addresses change. The game picks it up by querying for the address each draw.
    const int64_t rc = cd_->texture_reload(key.c_str());
    if (rc == RV_PCCD_NOT_RESIDENT) {
        dev_->reply(std::format("{} ok asset={} resident=0", req.id, rv_pcdev_hex(key)));
        return;
    }
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
    dev_->reply(std::format("{} ok asset={} resident=1 width={} height={}", req.id, rv_pcdev_hex(key),
        width, height));
}
