#ifndef RV_PDK_CV_RV_PIPELINE_H
#define RV_PDK_CV_RV_PIPELINE_H

#include <stdint.h>

// Bits of the `config` mask that rv_cv_frame_configure() takes.
//
// This file carries no declaration for cdef at all: it is nothing but constants,
// and contract constants live in #define (see the rule in pdk/README.md).
#define RV_PIPELINE_BUFFER_CONFIG_TYPE_Z UINT64_C(0x0000000000000001) // bit 0, per-pixel depth rejection

#endif // RV_PDK_CV_RV_PIPELINE_H
