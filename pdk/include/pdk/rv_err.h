#ifndef RV_PDK_RV_ERR_H
#define RV_PDK_RV_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

enum rv_err {
    RV_OK = 0,
    RV_ERR_INVAL = -1, // malformed call: bad argument, unknown handle, short buffer
    RV_ERR_NOMEM = -2, // a pool the controller manages is exhausted
    RV_ERR_BUSY = -3,  // the resource is occupied; retrying later may succeed
    RV_ERR_NOENT = -4, // the named thing does not exist
    RV_ERR_IO = -5,    // the device failed to carry out a well-formed call
};

/* Plain names: without a typedef, C demands `struct rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as an
   ordinary type. */
typedef enum rv_err rv_err;

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_RV_ERR_H
