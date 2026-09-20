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

// What a resource query names. A disc's resources are not only textures -
// sound and model are the obvious next kinds a PR review already asked for -
// but only the texture kind exists today, so it is the only enumerator.
// A second kind is added here and refused nowhere but the one gate named on
// rv_cd_resource_addr below, until it earns a record type of its own.
enum rv_cd_resource_kind {
    RV_CD_RESOURCE_TEXTURE = 1,
};

/* Plain name: without a typedef, C demands `enum rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as
   an ordinary type. */
typedef enum rv_cd_resource_kind rv_cd_resource_kind;

// A disc never acquires or releases a resource - it only ever names one.
// Residency is the drive's business: the first of these four calls to name a
// resource of a given kind makes it resident (for the texture kind: reading
// the asset, decoding its container and uploading it to video RAM), and
// every later call for the same name and kind reuses that upload. The drive
// keeps a resource resident until the disc that named it is unloaded, at
// which point the drive frees everything it made resident - the disc never
// frees anything itself.
// A name that is not on the disc, a resource that fails to decode, or a
// `kind` other than RV_CD_RESOURCE_TEXTURE all answer a negative rv_err from
// every one of the four (the unknown-kind case answers RV_ERR_INVAL - a
// malformed argument, distinct from the RV_ERR_NOENT an unknown NAME gets).
// A palette is texture-specific: palette_addr answers 0 for a texture format
// that has no palette, and would be nonsense for a future non-texture kind
// (a sound or a model has no palette) - that case is covered by the same
// kind refusal rather than machinery of its own.
// Addresses are valid until that resource is reloaded - ask again rather
// than caching across a reload.
int64_t rv_cd_resource_addr(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_palette_addr(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_width(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_height(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CD_RV_CD_H
