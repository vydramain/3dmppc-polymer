#ifndef RV_PDK_CV_RV_TEXTURE_H
#define RV_PDK_CV_RV_TEXTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

enum rv_texfmt {
    RV_TEXFMT_IDX4 = 4,      // 4-bit palette index  (16-colour palette)
    RV_TEXFMT_IDX8 = 8,      // 8-bit palette index  (256-colour palette)
    RV_TEXFMT_DIRECT15 = 15, // 15-bit direct colour + STP bit (no palette)
};

struct rv_texture {
    enum rv_texfmt format; // how to read `data` (and whether a palette is needed)

    const void *data; // texel bytes, owned by the game (main RAM)
    uint64_t size;    // length of `data`, in bytes
    uint64_t width;   // texel columns
    uint64_t height;  // texel rows
};

/* Plain names: without a typedef, C demands `struct rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as an
   ordinary type. */
typedef enum rv_texfmt rv_texfmt;
typedef struct rv_texture rv_texture;

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CV_RV_TEXTURE_H
