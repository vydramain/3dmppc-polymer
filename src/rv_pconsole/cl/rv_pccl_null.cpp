#include "rv_pconsole/cl/rv_pccl_null.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

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

} // namespace rv_3dmppc
