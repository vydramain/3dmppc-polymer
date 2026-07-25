// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 0 — видеопамять как тонкая обёртка над общим пулом консоли.
// ──────────────────────────────────────────────────────────────────────────────
//
// The video RAM the rv_cv contract promises: a fixed-size private pool the disc
// reserves regions in and gets opaque ADDRESSES back — never pointers.
//
// The allocator itself lives in rv_infra/rv_pcpool.hpp and is shared with sound
// RAM; what remains here is the part that is specific to VIDEO — the texture
// shape a region carries, and the contract's error vocabulary around it. A
// primitive names a region by address alone, so its format and dimensions have
// to be remembered next to the bytes; that is exactly the pool's Meta slot.
#pragma once

#include <cstdint>

#include "pdk/cv/rv_texture.hpp"
#include "rv_infra/rv_pcpool.hpp"

namespace rv_3dmppc {

// What a video region remembers about its last upload.
struct rv_pcvram_meta {
    bool written = false;
    rv_texfmt format = RV_TEXFMT_DIRECT15;
    int64_t width = 0;
    int64_t height = 0;
};

class rv_pcvram {
   public:
    explicit rv_pcvram(int64_t size);

    // Reserve `size` bytes. Returns the region address (> 0), or RV_ERR_INVAL
    // when `size` is not positive, or RV_ERR_NOMEM when no free block fits.
    int64_t malloc(int64_t size);

    // Release the region at `addr`. Returns RV_OK or RV_ERR_INVAL.
    int64_t free(int64_t addr);

    // Copy `texture.size` bytes of `texture.data` into the region at `addr` and
    // remember the texture's shape for the sampler. Returns RV_OK, or
    // RV_ERR_INVAL for an unknown address, a null source, or data that does not
    // fit the region.
    int64_t write(int64_t addr, const rv_texture& texture);

    // Is `addr` a live region? This is what rv_pccv::frame_put uses to reject a
    // primitive naming an address that was never handed out (or was freed).
    bool region_exists(int64_t addr) const;

    // The rv_texfmt a written region carries, or RV_ERR_NOENT when the region
    // exists but nothing has been uploaded into it yet (RV_ERR_INVAL when the
    // address is unknown). frame_put needs it to decide whether a palette
    // address is required.
    int64_t region_format(int64_t addr) const;

    // Shape of the uploaded texture, or a negative rv_err on the same terms as
    // region_format(). The sampler needs both to turn a uv into a texel offset.
    int64_t region_width(int64_t addr) const;
    int64_t region_height(int64_t addr) const;

    // Read-only view of a region's bytes, or nullptr when `addr` is not live.
    const uint8_t* region_data(int64_t addr) const;

    int64_t capacity() const { return pool_.capacity(); }

   private:
    // Shared helper: both accessors below need "is it live AND written".
    const rv_pcvram_meta* written_meta(int64_t addr) const;

    rv_pcpool<rv_pcvram_meta> pool_;
};

}  // namespace rv_3dmppc
