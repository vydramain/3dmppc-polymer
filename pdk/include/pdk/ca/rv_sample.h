#ifndef RV_PDK_CA_RV_SAMPLE_H
#define RV_PDK_CA_RV_SAMPLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

struct rv_sample {
    const void *data; // sample bytes, owned by the game (main RAM)
    int64_t size;     // length of `data`, in bytes
};

/* Plain names: without a typedef, C demands `struct rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as an
   ordinary type. */
typedef struct rv_sample rv_sample;

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CA_RV_SAMPLE_H
