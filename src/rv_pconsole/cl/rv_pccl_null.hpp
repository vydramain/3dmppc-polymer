// The rv_cl contract impersonated - except this one CANNOT: rv_cl_script_call
// promises retc results on the stack and rv_cl_stack_count counts them
// (pdk/include/pdk/cl/rv_cl.h); a machine with no VM has nowhere to put
// them. An honest RV_ERR_INVAL from every method is the correct Null Object
// here - a disc that needs scripts refuses to start when rv_cl_script_entry
// answers negative.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rv_pconsole/cl/rv_pccl.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"

namespace rv_3dmppc
{

class rv_pccl_null final : public rv_pccl
{
public:
    rv_pccl_null();

    // No VM: always 0 bytes.
    static rv_pcbudget_cost evaluate(const rv_pdklib::rv_manifest_budget &budget);

    int64_t script_load(const void *bytecode, int64_t size, const char *name) override;
    int64_t script_free(int64_t handle) override;
    int64_t script_entry() override;

    int64_t stack_push_nil() override;
    int64_t stack_push_boolean(bool value) override;
    int64_t stack_push_integer(int64_t value) override;
    int64_t stack_push_number(double value) override;
    int64_t stack_push_string(const char *text, int64_t length) override;
    int64_t stack_push_pointer(void *p) override;
    int64_t stack_drop(int64_t count) override;
    int64_t stack_count() override;

    int64_t value_type(int64_t index) override;
    int64_t value_boolean(int64_t index, bool *out) override;
    int64_t value_integer(int64_t index, int64_t *out) override;
    int64_t value_number(int64_t index, double *out) override;
    int64_t value_string(int64_t index, char *baddr, int64_t baddr_size) override;

    int64_t script_call(int64_t handle, const char *fname, int64_t argc, int64_t retc) override;

    // No VM, so there is nothing to reload and nothing to inspect. Each answers
    // a refusal with a named phase rather than pretending to succeed: a console
    // booted with --mode_cl=null and asked for a reload must say why, not report
    // ok on a machine that never ran a script.
    int64_t script_reload_entry(const void *bytecode, int64_t size, const char *name,
        rv_pccl_reload_report &report) override;
    int64_t script_reload_entry_from_drive(rv_pccl_reload_report &report) override;
    int64_t script_reload_module(const char *name, const void *bytecode, int64_t size,
        rv_pccl_reload_report &report) override;
    int64_t script_reload_module_from_drive(const char *name, rv_pccl_reload_report &report) override;
    int64_t state_get(const std::vector<std::string> &path, rv_pccl_value &out) override;
    int64_t state_keys(const std::vector<std::string> &path, rv_pccl_value &target,
        std::vector<rv_pccl_key> &out) override;
    int64_t state_collect(int64_t *used_out) override;
    void script_status(rv_pccl_status &out) const override;

    bool valid() const override
    {
        return true;
    }
};

} // namespace rv_3dmppc
