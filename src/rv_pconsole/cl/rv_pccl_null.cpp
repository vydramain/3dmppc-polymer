#include "rv_pconsole/cl/rv_pccl_null.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

rv_pcbudget_cost rv_pccl_null::evaluate(const rv_pdklib::rv_manifest_budget & /*budget*/)
{
    return {};
}

rv_pccl_null::rv_pccl_null()
{
    RV_LOG_INFO("pccl", "scripting off, no lua machine (every call answers RV_ERR_INVAL)");
}

int64_t rv_pccl_null::script_load(const void *, int64_t, const char *)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::script_free(int64_t)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::script_entry()
{
    return RV_ERR_INVAL;
}

int64_t rv_pccl_null::stack_push_nil()
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::stack_push_boolean(bool)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::stack_push_integer(int64_t)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::stack_push_number(double)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::stack_push_string(const char *, int64_t)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::stack_push_pointer(void *)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::stack_drop(int64_t)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::stack_count()
{
    return RV_ERR_INVAL;
}

int64_t rv_pccl_null::value_type(int64_t)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::value_boolean(int64_t, bool *)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::value_integer(int64_t, int64_t *)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::value_number(int64_t, double *)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::value_string(int64_t, char *, int64_t)
{
    return RV_ERR_INVAL;
}

int64_t rv_pccl_null::script_call(int64_t, const char *, int64_t, int64_t)
{
    return RV_ERR_INVAL;
}


// --- the development runtime -------------------------------------------------
// "no_machine" is a stable token: the client can tell "this console has no lua
// at all" from "your chunk did not compile" without reading a sentence.
int64_t rv_pccl_null::script_reload_entry(const void *, int64_t, const char *,
    rv_pccl_reload_report &report)
{
    report.phase = "no_machine";
    report.effects_possible = false;
    report.message = "this disc declared no lua machine";
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::script_reload_entry_from_drive(rv_pccl_reload_report &report)
{
    return script_reload_entry(nullptr, 0, nullptr, report);
}
int64_t rv_pccl_null::state_get(const std::vector<std::string> &, rv_pccl_value &)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::state_keys(const std::vector<std::string> &, rv_pccl_value &,
    std::vector<rv_pccl_key> &)
{
    return RV_ERR_INVAL;
}
int64_t rv_pccl_null::state_collect(int64_t *used_out)
{
    if (used_out != nullptr) {
        *used_out = 0;
    }
    return RV_ERR_INVAL;
}
// Every field stays at its default, and `reloadable` false is the honest
// summary: there is no entry chunk to reload.
void rv_pccl_null::script_status(rv_pccl_status &out) const
{
    out = rv_pccl_status{};
}

} // namespace rv_3dmppc
