// The rv_cv contract made abstract: argument validation, the error vocabulary,
// and addresses in and out. Exactly what "video" means underneath is a choice
// made by whichever concrete class the console picks — a real GPU over a real
// window (rv_pccv_sdl3) or a no-op that still reports the hardware shape the
// disc declared (rv_pccv_null).
#pragma once

#include <cstdint>
#include <string>

#include "pdk/cv/rv_primitives.h"
#include "pdk/cv/rv_texture.h"
#include "pdk/cv/rv_vertex.h"

namespace rv_3dmppc
{

class rv_pccv
{
public:
    virtual ~rv_pccv() = default;

    rv_pccv(const rv_pccv &) = delete;
    rv_pccv &operator=(const rv_pccv &) = delete;

    // --- hardware geometry: straight out of the configuration ---

    virtual int64_t screen_width() = 0;
    virtual int64_t screen_height() = 0;
    virtual int64_t texture_max_width() = 0;
    virtual int64_t texture_max_height() = 0;
    virtual int64_t video_memory_size() = 0;
    virtual int64_t frame_capacity() = 0;

    // --- video RAM ---

    virtual int64_t video_asset_malloc(int64_t size) = 0;
    virtual int64_t video_asset_write(int64_t addr, const rv_texture *texture) = 0;
    virtual int64_t video_asset_free(int64_t addr) = 0;

    // --- the frame ---

    virtual int64_t frame_configure(uint64_t config, rv_color clear_color) = 0;
    virtual int64_t frame_put(const rv_primitive *primitive) = 0;
    virtual int64_t frame_flush() = 0;

    // --- console-side: never reached through the extern "C" block ---

    // Does the memory this controller owns actually exist?
    virtual bool valid() const = 0;

    // Opens the machine's window (if it has one) at `title`/`scale`. Returns
    // RV_OK or RV_ERR_IO.
    virtual int64_t screen_open(const char *title, uint64_t scale) = 0;

    // True once the machine has a surface to present to.
    virtual bool presenting() const = 0;

    // Write the most recently presented frame to `path` as a binary PPM.
    // No-op when `path` is empty or nothing was ever presented.
    virtual void dump_last_frame(const std::string &path) const = 0;

protected:
    rv_pccv() = default;
};

} // namespace rv_3dmppc
