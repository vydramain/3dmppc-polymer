#include <cstdint>
#include <cstring>

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "rv_pcvmem.hpp"

#define RV_VMEM_CHECK(cond)                                      \
    do {                                                          \
        if (!(cond)) {                                            \
            RV_LOG_ERR("pcvmem", "selfcheck failed: {}", #cond); \
            return false;                                         \
        }                                                         \
    } while (0)

namespace rv_3dmppc
{

bool rv_pcvmem_selfcheck()
{
    {
        rv_pcvmem vmem(1 << 20); // 1 MiB
        RV_VMEM_CHECK(vmem.valid());
        RV_VMEM_CHECK(vmem.ensured() == 0);

        RV_VMEM_CHECK(vmem.ensure(4096) == RV_OK);
        uint8_t *p = vmem.base();
        p[0] = 0xAB;
        p[4095] = 0xCD;
        RV_VMEM_CHECK(p[0] == 0xAB);
        RV_VMEM_CHECK(p[4095] == 0xCD);

        const int64_t ensured_after_first = vmem.ensured();
        RV_VMEM_CHECK(vmem.ensure(1024) == RV_OK);
        RV_VMEM_CHECK(vmem.ensured() == ensured_after_first);

        const int64_t ensured_before_bad = vmem.ensured();
        RV_VMEM_CHECK(vmem.ensure(vmem.reserved() + 1) == RV_ERR_INVAL);
        RV_VMEM_CHECK(vmem.ensured() == ensured_before_bad);
    }

    {
        rv_pcvmem vmem(1 << 20);
        RV_VMEM_CHECK(vmem.ensure(4096) == RV_OK);
        uint8_t zeroes[4096] = {};
        RV_VMEM_CHECK(std::memcmp(vmem.base(), zeroes, sizeof(zeroes)) == 0);
    }

    return true;
}

} // namespace rv_3dmppc

#undef RV_VMEM_CHECK
