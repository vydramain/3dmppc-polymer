// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 3 — пул видеопамяти с first-fit аллокатором и коалесценцией.
// ──────────────────────────────────────────────────────────────────────────────
//
// The video RAM the rv_cv contract promises: a fixed-size private pool the disc
// reserves regions in and gets opaque ADDRESSES back — never pointers. The pool
// is a std::vector<uint8_t>, so the "addresses" are byte offsets into it and stay
// valid across host-heap reallocation (they are not pointers, so there is nothing
// to dangle).
//
// PATTERN: free-list allocator (object pool). One flat, offset-ordered vector of
// blocks covers the whole pool with no gaps; allocation splits a free block and
// release merges neighbours back. A general-purpose allocator would work too, but
// the console must be able to answer "is this address a live region?" for every
// primitive that names one, and that question is exactly what the block list is.
#pragma once

#include <cstdint>
#include <vector>

#include "pdk/cv/rv_texture.hpp"

namespace rv_3dmppc {

class rv_pcvram {
   public:
    explicit rv_pcvram(int64_t size);

    // Reserve `size` bytes. Returns the region address (> 0), or RV_ERR_INVAL
    // when `size` is not positive, or RV_ERR_NOMEM when no free block fits.
    int64_t malloc(int64_t size);

    // Release the region at `addr`. Returns RV_OK or RV_ERR_INVAL.
    int64_t free(int64_t addr);

    // Copy `texture.size` bytes of `texture.data` into the region at `addr` and
    // remember the texture's shape for the texturing stage. Returns RV_OK, or
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

    // Read-only view of a region's bytes, or nullptr when `addr` is unknown.
    // The texturing stage will sample through this; nothing does yet.
    const uint8_t* region_data(int64_t addr) const;

    int64_t capacity() const { return static_cast<int64_t>(pool_.size()); }

   private:
    // One span of the pool. `reserved` marks the head block that exists only to
    // keep address 0 out of circulation; it is never freed and never merged.
    struct rv_pcvram_block {
        int64_t offset = 0;
        int64_t size = 0;
        bool used = false;
        bool reserved = false;

        // Metadata of the last upload, kept next to the bytes because the
        // primitive that samples this region carries only the address.
        bool written = false;
        rv_texfmt format = RV_TEXFMT_DIRECT15;
        int64_t width = 0;
        int64_t height = 0;
    };

    int64_t find_block(int64_t addr) const;  // index into blocks_, or -1

    std::vector<uint8_t> pool_;
    std::vector<rv_pcvram_block> blocks_;  // offset-ordered, gapless
};

}  // namespace rv_3dmppc
