#ifndef RV_PDK_CV_RV_TEXEL_H
#define RV_PDK_CV_RV_TEXEL_H

#include <stdint.h>

// The fully transparent texel. Not a colour: a hole.
#define RV_TEXEL_TRANSPARENT ((uint16_t)0x0000)

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

// A colour already reduced to this machine's 5-bit-per-channel space — the space
// a DIRECT15 texel and a palette entry are actually stored in.
//
// It is a type of its own and not rv_color (rv_vertex.h) on purpose. Both are
// three bytes and they mean different things, so the compiler, not a comment,
// decides where the boundary is: anything that measures a distance between
// colours takes rv_color5, because measuring in 8-bit space would optimise a
// precision the hardware throws away a moment later.
struct rv_color5 {
    uint8_t r; // 0..31
    uint8_t g; // 0..31
    uint8_t b; // 0..31
};

/* Plain names: without a typedef, C demands `struct rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as an
   ordinary type. */
typedef struct rv_color5 rv_color5;

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CV_RV_TEXEL_H
