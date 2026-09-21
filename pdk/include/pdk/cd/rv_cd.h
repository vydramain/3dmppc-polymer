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
// the PR review that added this `kind` parameter asked for sound next, and
// model is still the obvious kind after that - so AUDIO is the second
// enumerator, refused nowhere but the one gate named on rv_cd_resource_addr
// below, the same way TEXTURE alone was until now.
enum rv_cd_resource_kind {
    RV_CD_RESOURCE_TEXTURE = 1,
    RV_CD_RESOURCE_AUDIO = 2,
};

/* Plain name: without a typedef, C demands `enum rv_x` at every mention and
   the contract would read as noise. It is also the shape ffi.cdef accepts as
   an ordinary type. */
typedef enum rv_cd_resource_kind rv_cd_resource_kind;

// A disc never acquires or releases a resource - it only ever names one.
// Residency is the drive's business: the first of these five calls to name a
// resource of a given kind makes it resident (for the texture kind: reading
// the asset, decoding its container and uploading it to video RAM; for the
// audio kind: reading the asset's raw PCM bytes - there is no container to
// decode, the game supplies none and none is baked - and uploading them to
// sound RAM), and every later call for the same name and kind reuses that
// upload. The drive keeps a resource resident until the disc that named it
// is unloaded, at which point the drive frees everything it made resident -
// the disc never frees anything itself.
//
// Not every one of these five means something for every kind:
//   * rv_cd_resource_addr          - meaningful for TEXTURE and AUDIO alike:
//                                    where the resident bytes now live.
//   * rv_cd_resource_size          - AUDIO only: its resident byte length,
//                                    the counterpart of a sample's rv_sample.size.
//                                    A texture's "size" is its width/height
//                                    below, not a byte count a game reads back.
//   * rv_cd_resource_palette_addr,
//     rv_cd_resource_width,
//     rv_cd_resource_height        - TEXTURE only: a sound has no palette and
//                                    no pixel dimensions.
// Asking a kind for a query that does not describe it is exactly as
// malformed as asking with a `kind` this enum has never heard of, and
// answers the same RV_ERR_INVAL rather than a bogus 0 that would read back
// as a real, if unlikely, "no palette" or "0 bytes" - a caller must be able
// to trust that a non-negative answer is real. This is deliberately still
// ONE refusal every resource_* query routes through (see rv_pccd_fs's
// resource_resolve_, the one gate every one of the five reaches), not five
// separate copies of the same check.
// A palette is texture-specific in the other direction too: palette_addr
// answers 0, not an error, for a texture FORMAT that has no palette - that
// is a real fact about a resident texture, not a malformed query - and only
// the kind mismatch above (asking palette_addr of an AUDIO name) is refused.
// A name that is not on the disc, or a texture resource that fails to
// decode (AUDIO has nothing to decode), also answers a negative rv_err from
// any of the five (RV_ERR_NOENT for the unknown name, distinct from the
// RV_ERR_INVAL a bad kind or a meaningless query gets).
// Addresses are valid until that resource is reloaded - ask again rather
// than caching across a reload.
int64_t rv_cd_resource_addr(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_size(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_palette_addr(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_width(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);
int64_t rv_cd_resource_height(rv_cd *cd, rv_cd_resource_kind kind, const char *resname);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CD_RV_CD_H
