#ifndef RV_PDK_CIO_RV_IMOUSE_H
#define RV_PDK_CIO_RV_IMOUSE_H

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

struct rv_imouse {
    int diff_x, diff_y;
};

/* Plain names: without a typedef, C demands `struct rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as an
   ordinary type. */
typedef struct rv_imouse rv_imouse;

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CIO_RV_IMOUSE_H
