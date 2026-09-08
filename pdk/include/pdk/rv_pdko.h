#ifndef RV_PDK_RV_PDKO_H
#define RV_PDK_RV_PDKO_H

#include "pdk/ca/rv_ca.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/cio/rv_cio.h"
#include "pdk/cm/rv_cm.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/cl/rv_cl.h"
#include "pdk/rv_err.h"   // IWYU pragma: keep (shared error vocabulary)
#include "pdk/de/rv_dv.h" // IWYU pragma: keep (version vocabulary)

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

typedef struct rv_pdko rv_pdko;

rv_ca *rv_pdko_ca(rv_pdko *o);   // sound chip (low-level SPU)
rv_cd *rv_pdko_cd(rv_pdko *o);   // disc drive - reads the mounted .mppcdisc
rv_cm *rv_pdko_cm(rv_pdko *o);   // memory card - persistent save slots
rv_cio *rv_pdko_cio(rv_pdko *o); // gamepads + haptic output + mouse
rv_cv *rv_pdko_cv(rv_pdko *o);   // GPU / rasterizer
rv_cl *rv_pdko_cl(rv_pdko *o);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_RV_PDKO_H
