// Run by --selfcheck, before any machine is brought up: catches a slot table
// wired wrong (a missing evaluate(), a null class that costs bytes, a posix
// card that forgot its own ceiling) as a boot-time assertion instead of a
// silent wrong number inside rv_pboot_check_budget.
#include "rv_pconsole/rv_pcslots.hpp"

#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace rv_3dmppc
{

namespace
{

template <typename Table>
bool every_row_has_evaluate(const Table &table, const char *slot)
{
    for (const auto &row : table) {
        if (row.evaluate == nullptr) {
            RV_LOG_ERR("pcslots", "slot '{}': row '{}' has no evaluate()", slot, row.name);
            return false;
        }
    }
    return true;
}

} // namespace

bool rv_pcslots_selfcheck()
{
    const rv_pdklib::rv_manifest_budget default_budget{};

    // Every row of every table names an evaluate().
    if (!every_row_has_evaluate(RV_PCSLOTS_CA, "ca") ||
        !every_row_has_evaluate(RV_PCSLOTS_CV, "cv") ||
        !every_row_has_evaluate(RV_PCSLOTS_CIO, "cio") ||
        !every_row_has_evaluate(RV_PCSLOTS_CL, "cl") ||
        !every_row_has_evaluate(RV_PCSLOTS_CD, "cd") ||
        !every_row_has_evaluate(RV_PCSLOTS_CM, "cm")) {
        return false;
    }

    // Every null class evaluates the default budget to RV_OK and 0 bytes.
    int64_t bytes = -1;
    if (rv_pcca_null::evaluate(default_budget, bytes) != RV_OK || bytes != 0) {
        RV_LOG_ERR("pcslots", "rv_pcca_null::evaluate() did not answer RV_OK/0");
        return false;
    }
    if (rv_pccv_null::evaluate(default_budget, bytes) != RV_OK || bytes != 0) {
        RV_LOG_ERR("pcslots", "rv_pccv_null::evaluate() did not answer RV_OK/0");
        return false;
    }
    if (rv_pccio_null::evaluate(default_budget, bytes) != RV_OK || bytes != 0) {
        RV_LOG_ERR("pcslots", "rv_pccio_null::evaluate() did not answer RV_OK/0");
        return false;
    }
    if (rv_pccl_null::evaluate(default_budget, bytes) != RV_OK || bytes != 0) {
        RV_LOG_ERR("pcslots", "rv_pccl_null::evaluate() did not answer RV_OK/0");
        return false;
    }
    if (rv_pccd_null::evaluate(default_budget, bytes) != RV_OK || bytes != 0) {
        RV_LOG_ERR("pcslots", "rv_pccd_null::evaluate() did not answer RV_OK/0");
        return false;
    }
    if (rv_pccm_null::evaluate(default_budget, bytes) != RV_OK || bytes != 0) {
        RV_LOG_ERR("pcslots", "rv_pccm_null::evaluate() did not answer RV_OK/0");
        return false;
    }

    // rv_pccm_posix refuses a budget whose card image is over the ceiling;
    // rv_pccm_null accepts the very same budget with 0 bytes.
    RV_LOG_INFO("pcslots", "selfcheck: the next pccheck error is an expected refusal");
    rv_pdklib::rv_manifest_budget oversized_budget{};
    oversized_budget.pccm.card_slots = 1;
    oversized_budget.pccm.card_slot_size = rv_pccard::RV_PCCARD_MAX_IMAGE_BYTES;
    if (rv_pccm_posix::evaluate(oversized_budget, bytes) == RV_OK) {
        RV_LOG_ERR("pcslots", "rv_pccm_posix::evaluate() accepted an over-ceiling card image");
        return false;
    }
    if (rv_pccm_null::evaluate(oversized_budget, bytes) != RV_OK || bytes != 0) {
        RV_LOG_ERR("pcslots",
            "rv_pccm_null::evaluate() did not accept the same over-ceiling budget at 0 bytes");
        return false;
    }

    // rv_pccv_sw evaluates the default budget to at least a vram pool plus a
    // bare framebuffer.
    if (rv_pccv_sw::evaluate(default_budget, bytes) != RV_OK) {
        RV_LOG_ERR("pcslots", "rv_pccv_sw::evaluate() refused the default budget");
        return false;
    }
    const int64_t floor = default_budget.pccv.video_memory_size +
        default_budget.pccv.screen_width * default_budget.pccv.screen_height *
            rv_pcfbuf::RV_PCFBUF_BYTES_PER_PIXEL;
    if (bytes < floor) {
        RV_LOG_ERR("pcslots",
            "rv_pccv_sw::evaluate() answered {} byte(s), under the {} byte(s) floor", bytes,
            floor);
        return false;
    }

    RV_LOG_INFO("pcslots", "selfcheck ok");
    return true;
}

} // namespace rv_3dmppc
