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

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CD_RV_CD_H
