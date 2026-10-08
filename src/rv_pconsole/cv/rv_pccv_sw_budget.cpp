#include "rv_pconsole/cv/rv_pccv_sw.hpp"

#include <cstdint>
#include <string>

#include "pdk/rv_err.h"
#include "rv_pconsole/cv/rv_pcfbuf.hpp"
#include "rv_pconsole/cv/rv_pcotable.hpp"
#include "rv_pconsole/cv/rv_pcvram.hpp"
#include "rv_pconsole/mem/rv_pcpool.hpp"
#include "rv_pconsole/rv_pcbudget.hpp"

namespace rv_3dmppc
{

int rv_pccv_sw::evaluate(const rv_pdklib::rv_manifest_budget &budget, int64_t &bytes, std::string &error)
{
    int64_t total = 0;

    // vram_ (rv_pcvram pool): exactly video_memory_size bytes.
    const int vram_add_rc = rv_pcbudget_add(total, "budget.pccv.video_memory_size", budget.pccv.video_memory_size, error);
    if (vram_add_rc != RV_OK) {
        return vram_add_rc;
    }

    // vram_'s block bookkeeping, reserved once at construction (rv_pcpool.hpp).
    int64_t vram_blocks_bytes = 0;
    const int vram_blocks_mul_rc = rv_pcbudget_mul("budget.pccv.video_memory_size",
        rv_pcpool<rv_pcvram_meta>::max_blocks(budget.pccv.video_memory_size, rv_pcvram::RV_PCVRAM_ALIGN),
        rv_pcpool<rv_pcvram_meta>::block_bytes(),
        vram_blocks_bytes,
        error);
    if (vram_blocks_mul_rc != RV_OK) {
        return vram_blocks_mul_rc;
    }

    const int vram_blocks_add_rc = rv_pcbudget_add(total, "budget.pccv.video_memory_size", vram_blocks_bytes, error);
    if (vram_blocks_add_rc != RV_OK) {
        return vram_blocks_add_rc;
    }

    // fbuf_ (rv_pcfbuf): width * height * bytes-per-pixel.
    int64_t pixels = 0;
    const int pixels_mul_rc = rv_pcbudget_mul("budget.pccv.screen_width * screen_height",
        budget.pccv.screen_width,
        budget.pccv.screen_height,
        pixels,
        error);
    if (pixels_mul_rc != RV_OK) {
        return pixels_mul_rc;
    }

    int64_t fbuf_bytes = 0;
    const int fbuf_mul_rc = rv_pcbudget_mul("budget.pccv.screen_width * screen_height * bytes_per_pixel",
        pixels,
        rv_pcfbuf::RV_PCFBUF_BYTES_PER_PIXEL,
        fbuf_bytes,
        error);
    if (fbuf_mul_rc != RV_OK) {
        return fbuf_mul_rc;
    }

    const int fbuf_add_rc =
        rv_pcbudget_add(total, "budget.pccv.screen_width * screen_height * bytes_per_pixel", fbuf_bytes, error);
    if (fbuf_add_rc != RV_OK) {
        return fbuf_add_rc;
    }

    // otable_ (rv_pcotable): head_/tail_ per bucket, plus one next_ link per
    // primitive up to frame_capacity.
    int64_t otable_bucket_bytes = 0;
    const int otable_bucket_mul_rc = rv_pcbudget_mul("budget.pccv.ot_bucket_count",
        budget.pccv.ot_bucket_count,
        rv_pcotable::RV_PCOTABLE_BYTES_PER_BUCKET,
        otable_bucket_bytes,
        error);
    if (otable_bucket_mul_rc != RV_OK) {
        return otable_bucket_mul_rc;
    }

    const int otable_bucket_add_rc = rv_pcbudget_add(total, "budget.pccv.ot_bucket_count", otable_bucket_bytes, error);
    if (otable_bucket_add_rc != RV_OK) {
        return otable_bucket_add_rc;
    }

    int64_t otable_next_bytes = 0;
    const int otable_next_mul_rc = rv_pcbudget_mul("budget.pccv.frame_capacity",
        budget.pccv.frame_capacity,
        rv_pcotable::RV_PCOTABLE_BYTES_PER_PRIMITIVE,
        otable_next_bytes,
        error);
    if (otable_next_mul_rc != RV_OK) {
        return otable_next_mul_rc;
    }

    const int otable_next_add_rc = rv_pcbudget_add(total, "budget.pccv.frame_capacity", otable_next_bytes, error);
    if (otable_next_add_rc != RV_OK) {
        return otable_next_add_rc;
    }

    // primitives_ (primitives_.reserve(frame_capacity)): frame_capacity * sizeof(rv_primitive).
    int64_t primitives_bytes = 0;
    const int primitives_mul_rc = rv_pcbudget_mul("budget.pccv.frame_capacity",
        budget.pccv.frame_capacity,
        static_cast<int64_t>(sizeof(rv_primitive)),
        primitives_bytes,
        error);
    if (primitives_mul_rc != RV_OK) {
        return primitives_mul_rc;
    }

    const int primitives_add_rc = rv_pcbudget_add(total, "budget.pccv.frame_capacity", primitives_bytes, error);
    if (primitives_add_rc != RV_OK) {
        return primitives_add_rc;
    }

    bytes = total;
    return RV_OK;
}

} // namespace rv_3dmppc
