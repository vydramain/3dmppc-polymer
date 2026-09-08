#ifndef RV_PDK_CL_RV_CL_H
#define RV_PDK_CL_RV_CL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

enum rv_cl_type {
    RV_CL_TYPE_NIL = 0,
    RV_CL_TYPE_BOOLEAN = 1,
    RV_CL_TYPE_NUMBER = 2,
    RV_CL_TYPE_STRING = 3,
    RV_CL_TYPE_FUNCTION = 4,
    RV_CL_TYPE_TABLE = 5,
    RV_CL_TYPE_OTHER = 6, // a value the contract has no vocabulary for
};

typedef struct rv_cl rv_cl;

int64_t rv_cl_script_load(rv_cl *cl, const void *bytecode, int64_t size, const char *name);
int64_t rv_cl_script_free(rv_cl *cl, int64_t handle);

int64_t rv_cl_stack_push_nil(rv_cl *cl);
int64_t rv_cl_stack_push_boolean(rv_cl *cl, bool value);
int64_t rv_cl_stack_push_integer(rv_cl *cl, int64_t value);
int64_t rv_cl_stack_push_number(rv_cl *cl, double value);
int64_t rv_cl_stack_push_string(rv_cl *cl, const char *text, int64_t length);
int64_t rv_cl_stack_drop(rv_cl *cl, int64_t count);
int64_t rv_cl_stack_count(rv_cl *cl);

int64_t rv_cl_value_type(rv_cl *cl, int64_t index);
int64_t rv_cl_value_boolean(rv_cl *cl, int64_t index, bool *out);
int64_t rv_cl_value_integer(rv_cl *cl, int64_t index, int64_t *out);
int64_t rv_cl_value_number(rv_cl *cl, int64_t index, double *out);

/// Copy a string value off the stack into a caller-owned buffer.
///
/// The string itself belongs to the script machine's collector, so it is never
/// handed out by pointer — it is copied out, and the caller supplies the room.
///
/// @param cl         the script machine
/// @param index      stack index of the value to read
/// @param baddr      buffer to copy into, or NULL to ask for the length alone
/// @param baddr_size capacity of `baddr` in bytes; 0 when `baddr` is NULL
/// @return the value's FULL length in bytes (not the number of bytes written),
///         or a negative rv_err. Compare the result against `baddr_size` to
///         learn what happened: `result <= baddr_size` means the value was
///         copied whole, `result > baddr_size` means it was truncated and the
///         result is the capacity needed to receive all of it. Calling with
///         (NULL, 0) therefore sizes the buffer without writing anything.
int64_t rv_cl_value_string(rv_cl *cl, int64_t index, char *baddr, int64_t baddr_size);

int64_t rv_cl_script_call(rv_cl *cl, int64_t handle, const char *fname, int64_t argc, int64_t retc);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CL_RV_CL_H
