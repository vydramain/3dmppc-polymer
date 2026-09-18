// The development channel's state inspection: `get` and `keys`. Dev slot only
// (see CMakeLists.txt); the dispatcher in rv_pconsole_dev.cpp calls into it.
#include "rv_pconsole/rv_pconsole.hpp"

#include <format>
#include <string>
#include <vector>

#include "pdk/cl/rv_cl.h"
#include "pdk/rv_err.h"
#include "rv_pconsole/cl/rv_pccl.hpp"
#include "rv_pconsole/platform/rv_pcdevchan.hpp"
#include "rv_pconsole/platform/rv_pcdevhex.hpp"

namespace
{

// A quarter of the answer queue, so one `keys` listing never crowds out the
// answers already queued around it.
constexpr int64_t RV_PCDEV_KEYS_LIST_MAX = rv_3dmppc::RV_PCDEVCHAN_OUT_MAX / 4;

// The type name `get` and `keys` both report; -1 (no such key) is "nil", the
// same fact a lua table stores no nil ever forces onto both commands.
const char *rv_pcdev_type_name(int64_t type)
{
    switch (type) {
    case RV_CL_TYPE_BOOLEAN:
        return "boolean";
    case RV_CL_TYPE_NUMBER:
        return "number";
    case RV_CL_TYPE_STRING:
        return "string";
    case RV_CL_TYPE_TABLE:
        return "table";
    case RV_CL_TYPE_FUNCTION:
        return "function";
    case RV_CL_TYPE_OTHER:
        return "other";
    default:
        return "nil";
    }
}

} // namespace

void rv_3dmppc::rv_pconsole::dev_get(const rv_pcdevreq &req)
{
    if (req.args.size() < 2) {
        dev_->reply(rv_pcdev_err(req.id, "protocol", RV_ERR_INVAL, false, "get needs a key"));
        return;
    }
    if (req.args.size() - 1 > RV_PCCL_STATE_PATH_MAX) {
        dev_->reply(rv_pcdev_err(req.id, "protocol", RV_ERR_INVAL, false,
            std::format("a path has at most {} segments", RV_PCCL_STATE_PATH_MAX)));
        return;
    }
    const std::vector<std::string> path(req.args.begin() + 1, req.args.end());

    rv_pccl_value value;
    const int64_t rc = cl_->state_get(path, value);
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
        dev_->reply(std::format("{} ok found=1 type=table count={}", req.id, value.count));
        return;
    case RV_CL_TYPE_FUNCTION:
        // Worth its own type name rather than "other": a function in state
        // keeps the old chunk's code alive and callable past a reload, which is
        // the one state-table mistake that looks like nothing at all.
        dev_->reply(std::format("{} ok found=1 type={}", req.id, rv_pcdev_type_name(value.type)));
        return;
    case RV_CL_TYPE_OTHER:
        dev_->reply(std::format("{} ok found=1 type={}", req.id, rv_pcdev_type_name(value.type)));
        return;
    default:
        // A lua table stores no nil, so "no such key" and "nil" are one fact.
        dev_->reply(std::format("{} ok found=0 type={}", req.id, rv_pcdev_type_name(value.type)));
        return;
    }
}

void rv_3dmppc::rv_pconsole::dev_keys(const rv_pcdevreq &req)
{
    if (req.args.size() - 1 > RV_PCCL_STATE_PATH_MAX) {
        dev_->reply(rv_pcdev_err(req.id, "protocol", RV_ERR_INVAL, false,
            std::format("a path has at most {} segments", RV_PCCL_STATE_PATH_MAX)));
        return;
    }
    const std::vector<std::string> path(req.args.begin() + 1, req.args.end());

    rv_pccl_value target;
    std::vector<rv_pccl_key> keys;
    const int64_t rc = cl_->state_keys(path, target, keys);
    if (rc == RV_ERR_NOMEM) {
        dev_->reply(rv_pcdev_err(req.id, "nomem", rc, false,
            "the script heap is exhausted; a key could not be interned. try gc"));
        return;
    }
    if (rc < 0) {
        dev_->reply(rv_pcdev_err(req.id, "no_machine", rc, false, "this disc declared no lua machine"));
        return;
    }

    const std::string prefix = std::format("{} ok found={} type={} count={} shown=", req.id,
        target.type < 0 ? 0 : 1, rv_pcdev_type_name(target.type), target.count);
    std::string list;
    int64_t shown = 0;
    for (const rv_pccl_key &k : keys) {
        std::string entry;
        switch (k.kind) {
        case 's':
            entry = std::format("s{}:{}", rv_pcdev_hex(k.name), rv_pcdev_type_name(k.type));
            break;
        case 'i':
            entry = std::format("i{}:{}", k.name, rv_pcdev_type_name(k.type));
            break;
        default:
            entry = std::format("x:{}", rv_pcdev_type_name(k.type));
            break;
        }
        const std::size_t added = entry.size() + (list.empty() ? 0 : 1);
        if (static_cast<int64_t>(prefix.size() + list.size() + added + std::string("keys=").size()) >
            RV_PCDEV_KEYS_LIST_MAX) {
            break;
        }
        if (!list.empty()) {
            list.push_back(',');
        }
        list += entry;
        ++shown;
    }

    dev_->reply(std::format("{}{} keys={}", prefix, shown, list));
}
