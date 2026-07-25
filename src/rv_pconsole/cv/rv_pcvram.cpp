// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 0 — видеопамять поверх rv_pcpool: форма текстуры и коды ошибок.
// ──────────────────────────────────────────────────────────────────────────────
#include "rv_pconsole/cv/rv_pcvram.hpp"

#include "pdk/rv_err.hpp"

namespace rv_3dmppc {
namespace {

// Every region starts on a 4-byte boundary. The pool holds 16-bit texels and
// 16-bit palette entries; an unaligned region would make the sampler pay for
// byte-wise reads on every texel fetch.
constexpr int64_t RV_PCVRAM_ALIGN = 4;

// The pool's first bytes are never handed out, so no live region can ever have
// address 0 — see the constructor comment in rv_pcpool.hpp for why that matters
// to a zero-initialized rv_polygon.
constexpr int64_t RV_PCVRAM_RESERVED_HEAD = 16;

}  // namespace

rv_pcvram::rv_pcvram(int64_t size) : pool_(size, RV_PCVRAM_ALIGN, RV_PCVRAM_RESERVED_HEAD) {}

int64_t rv_pcvram::malloc(int64_t size) { return pool_.malloc(size); }

int64_t rv_pcvram::free(int64_t addr) { return pool_.free(addr); }

int64_t rv_pcvram::write(int64_t addr, const rv_texture& texture) {
    const int64_t rc = pool_.write(addr, texture.data, static_cast<int64_t>(texture.size));
    if (rc < 0) return rc;

    // Shape is recorded only after the bytes landed: a failed upload must not
    // leave the region claiming a texture it does not hold.
    rv_pcvram_meta* meta = pool_.region_meta(addr);
    if (!meta) return RV_ERR_INVAL;

    meta->written = true;
    meta->format = texture.format;
    meta->width = static_cast<int64_t>(texture.width);
    meta->height = static_cast<int64_t>(texture.height);

    return RV_OK;
}

bool rv_pcvram::region_exists(int64_t addr) const { return pool_.region_exists(addr); }

const rv_pcvram_meta* rv_pcvram::written_meta(int64_t addr) const {
    const rv_pcvram_meta* meta = pool_.region_meta(addr);
    return (meta && meta->written) ? meta : nullptr;
}

int64_t rv_pcvram::region_format(int64_t addr) const {
    if (!pool_.region_exists(addr)) return RV_ERR_INVAL;

    const rv_pcvram_meta* meta = written_meta(addr);
    // The region is real but empty — a distinct answer from "no such region",
    // because frame_put uses it to decide whether a palette is required and an
    // unwritten region cannot demand one.
    if (!meta) return RV_ERR_NOENT;

    return static_cast<int64_t>(meta->format);
}

int64_t rv_pcvram::region_width(int64_t addr) const {
    if (!pool_.region_exists(addr)) return RV_ERR_INVAL;
    const rv_pcvram_meta* meta = written_meta(addr);
    return meta ? meta->width : static_cast<int64_t>(RV_ERR_NOENT);
}

int64_t rv_pcvram::region_height(int64_t addr) const {
    if (!pool_.region_exists(addr)) return RV_ERR_INVAL;
    const rv_pcvram_meta* meta = written_meta(addr);
    return meta ? meta->height : static_cast<int64_t>(RV_ERR_NOENT);
}

const uint8_t* rv_pcvram::region_data(int64_t addr) const { return pool_.region_data(addr); }

}  // namespace rv_3dmppc
