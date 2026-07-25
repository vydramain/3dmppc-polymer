// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 3 — контроллер видео: видеопамять, кадр-как-команды, флаш.
// ──────────────────────────────────────────────────────────────────────────────
//
// The console's rv_cv implementation: the "GPU". It owns the four pieces a frame
// needs — the video RAM pool, the ordering table, the framebuffer and the
// per-frame primitive buffer — and hands the finished page to the SDL host.
//
// PATTERN: retained-mode command buffer. frame_put() does not draw: it validates
// the primitive, COPIES it into a frame-owned vector and files its index in the
// ordering table. Nothing is rasterized until frame_flush(). That is forced by
// the contract (pdk/cv/rv_cv.hpp): the console re-orders primitives by depth, so
// it cannot know a primitive's turn to draw until every primitive has arrived.
// The copy is what makes each command self-contained — the disc may free or
// overwrite its own primitive struct the moment frame_put() returns, exactly as
// with video_asset_write().
//
// PATTERN: facade. Every method here is contract semantics (validation, error
// codes, frame lifecycle) layered over a delegation to one of the four workers;
// none of the mechanism — first-fit allocation, bucket sort, RGB555 packing,
// scanline filling — lives in this class. rv_cv is the vocabulary a disc speaks;
// rv_pcvram / rv_pcotable / rv_pcfbuf / rv_pcraster are the machine's parts, and
// they never learn about each other.
//
// DEFERRED: texture sampling. A primitive may legally ask for it (the addresses
// are validated as the contract requires), it simply rasterizes as a flat fill
// and the frame reports it once — see the warning counter below.
#pragma once

#include <cstdint>
#include <vector>

#include "pdk/cv/rv_cv.hpp"
#include "pdk/cv/rv_primitives.hpp"
#include "pdk/cv/rv_texture.hpp"
#include "pdk/cv/rv_vertex.hpp"
#include "rv_pconsole/cv/rv_pcfbuf.hpp"
#include "rv_pconsole/cv/rv_pcotable.hpp"
#include "rv_pconsole/cv/rv_pcvram.hpp"
#include "rv_pconsole/rv_pchost.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc {

class rv_pccv : public rv_cv {
   public:
    // `host` is borrowed: the console owns it, constructs it before every
    // controller and tears it down after them (see rv_pconsole.hpp — the
    // declaration order there is load-bearing).
    rv_pccv(const rv_pccv_conf& conf, rv_pchost& host);
    ~rv_pccv() override = default;

    // The frame buffers below are screen-sized pages and the vram pool is a
    // megabyte: copying a console's GPU is never meaningful, and `host_` is a
    // reference, so let the compiler say so instead of silently slicing.
    rv_pccv(const rv_pccv&) = delete;
    rv_pccv& operator=(const rv_pccv&) = delete;

    // --- hardware geometry: straight out of the configuration ---

    int64_t screen_width() override;
    int64_t screen_height() override;
    int64_t texture_max_width() override;
    int64_t texture_max_height() override;
    int64_t video_memory_size() override;
    int64_t frame_capacity() override;

    // --- video RAM ---

    int64_t video_asset_malloc(int64_t size) override;
    int64_t video_asset_write(int64_t addr, rv_texture texture) override;
    int64_t video_asset_free(int64_t addr) override;

    // --- the frame ---

    int64_t frame_configure(uint64_t config, rv_color clear_color) override;
    int64_t frame_put(rv_primitive primitive) override;
    int64_t frame_flush() override;

   private:
    // Contract validation of the fill attributes a polygon and a sprite share.
    // Returns RV_OK or RV_ERR_INVAL. Const because it only interrogates the
    // vram pool — filing the primitive is the caller's job.
    int64_t check_fill(uint32_t fill_mode, int64_t addr_texture, int64_t addr_palette) const;

    // Is `format` one of the rv_texfmt enumerators this console knows?
    static bool texture_format_known(rv_texfmt format);

    // Drop the frame's commands and their ordering. Does NOT touch the clear
    // colour or the Z flag — frame_configure sets those and then calls this.
    void frame_reset();

    rv_pccv_conf conf_;
    rv_pchost& host_;  // borrowed, never owned

    rv_pcfbuf fbuf_;
    rv_pcvram vram_;
    rv_pcotable otable_;

    // The frame's commands, in submission order. The ordering table stores
    // indexes into this vector, so it must not be reordered mid-frame.
    std::vector<rv_primitive> primitives_;

    // --- frame state, valid between frame_configure and frame_flush ---

    rv_color clear_color_{0, 0, 0};  // default: a black frame
    bool z_enabled_ = false;         // default: ordering table only

    // How many primitives of THIS frame asked for the DEFERRED texture path.
    // The counter lives here rather than in the rasterizer because the
    // rasterizer is a stateless service (rv_pcraster.hpp) and could not tell a
    // frame's first textured primitive from its thousandth — which is exactly
    // what "warn once per frame, not once per primitive" needs to know.
    int64_t texture_requests_ = 0;
};

}  // namespace rv_3dmppc
