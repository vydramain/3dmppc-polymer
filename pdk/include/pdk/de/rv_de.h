#ifndef RV_PDK_DE_RV_DE_H
#define RV_PDK_DE_RV_DE_H

#include <stdint.h>

// A FORWARD declaration, not #include "pdk/rv_pdko.h": the facade header pulls
// in rv_dv.h, which pulls in this one, and a full include would close the ring.
// To declare its table of hooks a disc needs no definition of rv_pdko at all —
// only a pointer to it.

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

typedef struct rv_pdko rv_pdko;

typedef struct rv_de rv_de;

struct rv_de {
    void *self;
    int64_t (*disc_initialize)(void *self, rv_pdko *pdk);
    void (*frame_update)(void *self, float dt);
    void (*frame_render)(void *self);
    int (*disc_release)(void *self); /* 0 = keep running, 1 = the disc asks to stop */
    void (*disc_shutdown)(void *self);
    const char *(*disc_title)(void *self);
};

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_DE_RV_DE_H
