// The machine with no video: no window, no framebuffer, no VRAM, no
// rasterization. What cv=null means: the rv_cv slot's Null Object instead of a
// branch inside rv_pccv_sdl3.
#pragma once

#include <cstdint>
#include <string>

#include "pdk/cv/rv_texture.h"
#include "rv_pconsole/cv/rv_pccv.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

class rv_pccv_null final : public rv_pccv
{
public:
    explicit rv_pccv_null(const rv_pccv_conf &conf);

    // --- hardware geometry: straight out of the configuration ---

    int64_t screen_width() override;
    int64_t screen_height() override;
    int64_t texture_max_width() override;
    int64_t texture_max_height() override;
    int64_t video_memory_size() override;
    int64_t frame_capacity() override;

    // --- video RAM: no pool, nothing to validate ---

    int64_t video_asset_malloc(int64_t size) override;
    int64_t video_asset_write(int64_t addr, const rv_texture *texture) override;
    int64_t video_asset_free(int64_t addr) override;

    // --- the frame: nothing to buffer, nothing to draw ---

    int64_t frame_configure(uint64_t config, rv_color clear_color) override;
    int64_t frame_put(const rv_primitive *primitive) override;
    int64_t frame_flush() override;

    // --- console-side ---

    bool valid() const override
    {
        return true;
    }

    int64_t screen_open(const char *title, uint64_t scale) override;

    bool presenting() const override
    {
        return false;
    }

    void dump_last_frame(const std::string &path) const override;

private:
    rv_pccv_conf conf_;
};

} // namespace rv_3dmppc
