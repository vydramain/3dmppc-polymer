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

// A disc never acquires or releases a texture - it only ever names one.
// Residency is the drive's business: the first of these four calls to name a
// texture makes it resident in video RAM (reading the asset, decoding its
// container and uploading it), and every later call for the same name reuses
// that upload. The drive keeps a texture resident until the disc that named
// it is unloaded, at which point the drive frees everything it made
// resident - the disc never frees anything itself.
// A name that is not on the disc, or a texture that fails to decode, answers
// the same negative rv_err from all four (the same one asset_open would give
// that name). palette_addr answers 0 for a format that has no palette.
// Addresses are valid until that texture is reloaded - ask again rather than
// caching across a reload.
int64_t rv_cd_texture_addr(rv_cd *cd, const char *resname);
int64_t rv_cd_texture_palette_addr(rv_cd *cd, const char *resname);
int64_t rv_cd_texture_width(rv_cd *cd, const char *resname);
int64_t rv_cd_texture_height(rv_cd *cd, const char *resname);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CD_RV_CD_H
