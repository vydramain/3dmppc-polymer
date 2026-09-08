#ifndef RV_PDK_CA_RV_LOOP_H
#define RV_PDK_CA_RV_LOOP_H

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

enum rv_loop {
    RV_LOOP_NONE = 0,    // play once, then the voice goes silent (one-shot)
    RV_LOOP_FOREVER = 1, // repeat the whole sample indefinitely
                         // RV_LOOP_SUSTAIN,
                         // DEFERRED: loop until voice_stop, then run the release phase
};

/* Plain names: without a typedef, C demands `struct rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as an
   ordinary type. */
typedef enum rv_loop rv_loop;

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CA_RV_LOOP_H
