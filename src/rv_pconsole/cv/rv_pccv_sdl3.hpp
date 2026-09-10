// The console's rv_cv implementation: the "GPU". It owns the four pieces a frame
// needs — the video RAM pool, the ordering table, the framebuffer and the
// per-frame primitive buffer — and hands the finished page to the SDL host.
//
// PATTERN: retained-mode command buffer. frame_put() does not draw: it validates
// the primitive, COPIES it into a frame-owned vector and files its index in the
// ordering table. Nothing is rasterized until frame_flush(). That is forced by
// the contract (pdk/cv/rv_cv.h): the console re-orders primitives by depth, so
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
// PATTERN: address resolution at the boundary. This class is the ONLY one that
// knows an rv_polygon::addr_texture is a video RAM address: at flush it turns
// the addresses into an rv_pctexview (borrowed pointers plus a shape) and hands
// that down. The rasterizer and the sampler below it stay pool-free, and the
// pool's "who may hold a pointer into it, and for how long" rule stays inside
// the one class that owns the pool.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pdk/cv/rv_primitives.h"
#include "pdk/cv/rv_vertex.h"
#include "pdk/cv/rv_texture.h"
#include "pdk/cv/rv_vertex.h"
#include "rv_pconsole/cv/rv_pcfbuf.hpp"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/cv/rv_pcotable.hpp"
#include "rv_pconsole/cv/rv_pctexel.hpp"
#include "rv_pconsole/cv/rv_pcvram.hpp"
#include "rv_pconsole/rv_pchost_sdl3.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

// Same reasoning as rv_pchost_sdl3.hpp: the presentation SDL handles are
// forward-declared here rather than pulled in via <SDL3/SDL.h>, so this
// header stays SDL-free; the dependency stops at rv_pccv_sdl3.cpp.
struct SDL_Renderer;
struct SDL_Texture;

namespace rv_3dmppc
{

class rv_pccv_sdl3 final : public rv_pccv
{
public:
    // `host` is borrowed: the console owns it, constructs it before every
    // controller and tears it down after them (see rv_pconsole.hpp — the
    // declaration order there is load-bearing).
    rv_pccv_sdl3(const rv_pccv_conf &conf, rv_pchost_sdl3 &host);

    // PATTERN: RAII. The only teardown path for the renderer and the streaming
    // texture — destroyed texture-then-renderer. The host, which owns the
    // window they were built on, is declared before the console's controllers
    // (rv_pconsole.hpp) and so outlives this destructor call.
    ~rv_pccv_sdl3() override;

    // The frame buffers below are screen-sized pages and the vram pool is a
    // megabyte: copying a console's GPU is never meaningful, and `host_` is a
    // reference, so let the compiler say so instead of silently slicing.
    rv_pccv_sdl3(const rv_pccv_sdl3 &) = delete;
    rv_pccv_sdl3 &operator=(const rv_pccv_sdl3 &) = delete;

    // --- hardware geometry: straight out of the configuration ---

    int64_t screen_width() override;
    int64_t screen_height() override;
    int64_t texture_max_width() override;
    int64_t texture_max_height() override;
    int64_t video_memory_size() override;
    int64_t frame_capacity() override;

    // --- video RAM ---

    int64_t video_asset_malloc(int64_t size) override;
    int64_t video_asset_write(int64_t addr, const rv_texture *texture) override;
    int64_t video_asset_free(int64_t addr) override;

    // --- the frame ---

    int64_t frame_configure(uint64_t config, rv_color clear_color) override;
    int64_t frame_put(const rv_primitive *primitive) override;
    int64_t frame_flush() override;

    // --- presentation ---
    //
    // Owns the surface a finished frame lands on: opens the host's window,
    // then builds the renderer and streaming texture on top of it. Not called
    // when cv=null.

    // Opens `host_`'s window at `title`/`scale` and, if that succeeds, the
    // renderer and streaming texture presented frames land in. Returns RV_OK
    // or RV_ERR_IO.
    int64_t screen_open(const char *title, uint64_t scale) override;

    // True once the machine has a surface to present to.
    bool presenting() const override
    {
        return renderer_ != nullptr;
    }

    // Write the most recently presented frame to `path` as a binary PPM. A
    // developer convenience: it makes "what did the console actually draw" a
    // file that can be diffed, instead of a screen capture that cannot.
    // No-op when `path` is empty or nothing was ever presented.
    void dump_last_frame(const std::string &path) const override;

    // Does the memory this controller owns actually exist? Only the vram pool
    // can fail here — the frame buffer and ordering table size from the same
    // configuration but never reserve host memory that can be refused.
    bool valid() const override
    {
        return vram_.valid();
    }

private:
    // Contract validation of the fill attributes a polygon and a sprite share.
    // Returns RV_OK or RV_ERR_INVAL. Const because it only interrogates the
    // vram pool — filing the primitive is the caller's job.
    int64_t check_fill(uint32_t fill_mode, int64_t addr_texture, int64_t addr_palette) const;

    // Is `format` one of the rv_texfmt enumerators this console knows?
    static bool texture_format_known(rv_texfmt format);

    // Resolve the addresses a primitive names into a view the rasterizer can
    // sample. Returns an INVALID view when the primitive does not sample, when
    // the region was never uploaded into, or when an indexed format's palette is
    // missing — never an error, because frame_put already reported everything
    // the disc can still act on and a flush has no error channel back to it.
    //
    // The view borrows pointers into the pool. They are valid only for the
    // duration of the draw call: the disc cannot free a region mid-flush (it is
    // not running), so the shortest possible lifetime is also a safe one.
    rv_pctexview texture_view(const rv_primitive &primitive) const;

    // The texture / palette pair a primitive samples, or (0, 0) when it does not
    // sample at all. Shared by polygons and sprites, which name their assets
    // identically.
    static void texture_addresses(const rv_primitive &primitive, int64_t &addr_texture,
        int64_t &addr_palette);

    // Drop the frame's commands and their ordering. Does NOT touch the clear
    // colour or the Z flag — frame_configure sets those and then calls this.
    void frame_reset();

    // Hand a finished frame to the display. `argb` is width*height pixels in
    // 0xAARRGGBB, produced by rv_pcfbuf::expand_argb().
    void present(const uint32_t *argb);

    rv_pccv_conf conf_;
    rv_pchost_sdl3 &host_; // borrowed, never owned

    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;

    // BORROWED from fbuf_ (which outlives this class); only ever read inside
    // dump_last_frame(). Nothing is copied per frame — presenting must stay
    // cheap.
    const uint32_t *last_frame_ = nullptr;

    rv_pcfbuf fbuf_;
    rv_pcvram vram_;
    rv_pcotable otable_;

    // The frame's commands, in submission order. The ordering table stores
    // indexes into this vector, so it must not be reordered mid-frame.
    std::vector<rv_primitive> primitives_;

    // --- frame state, valid between frame_configure and frame_flush ---

    rv_color clear_color_{ 0, 0, 0 }; // default: a black frame
    bool z_enabled_ = false;          // default: ordering table only
};

} // namespace rv_3dmppc
