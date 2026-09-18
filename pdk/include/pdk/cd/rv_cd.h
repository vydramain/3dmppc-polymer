#ifndef RV_PDK_CD_RV_CD_H
#define RV_PDK_CD_RV_CD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

typedef struct rv_cd rv_cd;

int64_t rv_cd_asset_open(rv_cd *cd, const char *resname);
int64_t rv_cd_asset_size(rv_cd *cd, int64_t handle);
int64_t rv_cd_asset_read(rv_cd *cd, int64_t handle, void *baddr, int64_t baddr_size);

// acquire: reads the named asset, decodes its container and makes it resident
// in video RAM; returns a residency id or a negative rv_err. Two acquires of
// one name share the residency and count references.
// release: drops one reference; the video allocation is freed at zero.
// Releasing twice is RV_ERR_INVAL.
// The four getters answer what a primitive needs in order to draw a texture.
// A residency id is never reused within a run, so a stale id always errors
// instead of naming someone else's texture. palette_addr answers 0 for a
// format that has no palette. Addresses are valid until that texture is
// reloaded - ask again rather than caching across a reload.
int64_t rv_cd_texture_acquire(rv_cd *cd, const char *resname);
int64_t rv_cd_texture_release(rv_cd *cd, int64_t res);
int64_t rv_cd_texture_addr(rv_cd *cd, int64_t res);
int64_t rv_cd_texture_palette_addr(rv_cd *cd, int64_t res);
int64_t rv_cd_texture_width(rv_cd *cd, int64_t res);
int64_t rv_cd_texture_height(rv_cd *cd, int64_t res);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CD_RV_CD_H
