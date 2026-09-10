#include "rv_pconsole/cl/rv_pccl.hpp"

#include "pdk/cl/rv_cl.h"

// --- C contract (pdk/cl/rv_cl.h): an rv_cl* IS the address of an rv_pccl ----
extern "C" int64_t rv_cl_script_load(rv_cl *cl, const void *bytecode, int64_t size, const char *name)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->script_load(bytecode, size, name);
}
extern "C" int64_t rv_cl_script_free(rv_cl *cl, int64_t handle)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->script_free(handle);
}
extern "C" int64_t rv_cl_script_entry(rv_cl *cl)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->script_entry();
}
extern "C" int64_t rv_cl_stack_push_nil(rv_cl *cl)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->stack_push_nil();
}
extern "C" int64_t rv_cl_stack_push_boolean(rv_cl *cl, bool value)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->stack_push_boolean(value);
}
extern "C" int64_t rv_cl_stack_push_integer(rv_cl *cl, int64_t value)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->stack_push_integer(value);
}
extern "C" int64_t rv_cl_stack_push_number(rv_cl *cl, double value)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->stack_push_number(value);
}
extern "C" int64_t rv_cl_stack_push_string(rv_cl *cl, const char *text, int64_t length)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->stack_push_string(text, length);
}
extern "C" int64_t rv_cl_stack_push_pointer(rv_cl *cl, void *p)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->stack_push_pointer(p);
}
extern "C" int64_t rv_cl_stack_drop(rv_cl *cl, int64_t count)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->stack_drop(count);
}
extern "C" int64_t rv_cl_stack_count(rv_cl *cl)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->stack_count();
}
extern "C" int64_t rv_cl_value_type(rv_cl *cl, int64_t index)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->value_type(index);
}
extern "C" int64_t rv_cl_value_boolean(rv_cl *cl, int64_t index, bool *out)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->value_boolean(index, out);
}
extern "C" int64_t rv_cl_value_integer(rv_cl *cl, int64_t index, int64_t *out)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->value_integer(index, out);
}
extern "C" int64_t rv_cl_value_number(rv_cl *cl, int64_t index, double *out)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->value_number(index, out);
}
extern "C" int64_t rv_cl_value_string(rv_cl *cl, int64_t index, char *baddr, int64_t baddr_size)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->value_string(index, baddr, baddr_size);
}
extern "C" int64_t rv_cl_script_call(rv_cl *cl, int64_t handle, const char *fname, int64_t argc,
    int64_t retc)
{
    return reinterpret_cast<rv_3dmppc::rv_pccl *>(cl)->script_call(handle, fname, argc, retc);
}
