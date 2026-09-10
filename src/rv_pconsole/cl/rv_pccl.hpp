// The rv_cl contract made abstract: script loading, the stack protocol, and
// the error vocabulary. Exactly what "running a script" means underneath is
// a choice made by whichever concrete class the console picks — a real lua
// machine (rv_pccl_luajit) or a no-op that answers RV_ERR_INVAL to every
// call (rv_pccl_null).
#pragma once

#include <cstdint>

namespace rv_3dmppc
{

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

    // Does the machine this controller owns actually exist? Console-side
    // only — not reached through the extern "C" block.
    virtual bool valid() const = 0;

protected:
    rv_pccl() = default;
};

} // namespace rv_3dmppc
