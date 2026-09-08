#ifndef RV_PDK_CM_RV_CM_H
#define RV_PDK_CM_RV_CM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

typedef struct rv_cm rv_cm;

int64_t rv_cm_card_slots(rv_cm *cm);
int64_t rv_cm_card_slot_size(rv_cm *cm);
int64_t rv_cm_card_size(rv_cm *cm, int64_t slot);
int64_t rv_cm_card_read(rv_cm *cm, int64_t slot, void *baddr, int64_t baddr_size);
int64_t rv_cm_card_write(rv_cm *cm, int64_t slot, const void *data, int64_t data_size);
int64_t rv_cm_card_erase(rv_cm *cm, int64_t slot);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CM_RV_CM_H
