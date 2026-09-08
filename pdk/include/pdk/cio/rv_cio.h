#ifndef RV_PDK_CIO_RV_CIO_H
#define RV_PDK_CIO_RV_CIO_H

#include <stdint.h>

#include "pdk/cio/rv_imouse.h"
#include "pdk/cio/rv_isource.h"
#include "pdk/cio/rv_ohaptic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RV_CDEF_BEGIN */

typedef struct rv_cio rv_cio;

int64_t rv_cio_iport_count(rv_cio *cio);
rv_istate rv_cio_iport_state(rv_cio *cio, int64_t port);
uint64_t rv_cio_iport_abilities(rv_cio *cio, int64_t port);

rv_imouse rv_cio_imouse(rv_cio *cio);

int64_t rv_cio_ohaptic(rv_cio *cio, int64_t port, rv_oheffect effect);

/* RV_CDEF_END */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // RV_PDK_CIO_RV_CIO_H
