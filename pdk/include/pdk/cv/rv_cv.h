#ifndef RV_PDK_CV_RV_CV_H
#define RV_PDK_CV_RV_CV_H

#include <stdint.h>

#include "pdk/cv/rv_pipeline.h" // IWYU pragma: keep (frame_configure flag vocabulary)
#include "pdk/cv/rv_primitives.h"
#include "pdk/cv/rv_texture.h"
#include "pdk/cv/rv_vertex.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

typedef struct rv_cv rv_cv;

int64_t rv_cv_screen_width(rv_cv *cv);
int64_t rv_cv_screen_height(rv_cv *cv);

int64_t rv_cv_texture_max_width(rv_cv *cv);
int64_t rv_cv_texture_max_height(rv_cv *cv);

int64_t rv_cv_video_memory_size(rv_cv *cv);

int64_t rv_cv_video_asset_malloc(rv_cv *cv, int64_t size);
int64_t rv_cv_video_asset_write(rv_cv *cv, int64_t addr, const rv_texture *texture);
int64_t rv_cv_video_asset_free(rv_cv *cv, int64_t addr);

int64_t rv_cv_frame_capacity(rv_cv *cv);
int64_t rv_cv_frame_configure(rv_cv *cv, uint64_t config, rv_color clear_color);
int64_t rv_cv_frame_put(rv_cv *cv, const rv_primitive *primitive);
int64_t rv_cv_frame_flush(rv_cv *cv);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CV_RV_CV_H
