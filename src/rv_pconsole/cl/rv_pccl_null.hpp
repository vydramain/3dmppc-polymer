// The rv_cl contract impersonated — except this one CANNOT: rv_cl_script_call
// promises retc results on the stack and rv_cl_stack_count counts them
// (pdk/include/pdk/cl/rv_cl.h); a machine with no VM has nowhere to put
// them. An honest RV_ERR_INVAL from every method is the correct Null Object
// here — a disc that needs scripts refuses to start when rv_cl_script_entry
// answers negative.
#pragma once

#include <cstdint>

#include "rv_pconsole/cl/rv_pccl.hpp"

namespace rv_3dmppc
{

class rv_pccl_null final : public rv_pccl
{
public:
    rv_pccl_null();

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

    bool valid() const override
    {
        return true;
    }
};

} // namespace rv_3dmppc
