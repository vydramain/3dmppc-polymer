#pragma once

#include "pdk/cv/rv_cv.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc {
class rv_pccv : public rv_cv {
   private:
    rv_pccv_conf conf_;

   public:
    rv_pccv(const rv_pccv_conf conf) : conf_(conf) {}
    ~rv_pccv() = default;

    int64_t screen_width() override;

    int64_t screen_height() override;

    int64_t texture_max_width() override;

    int64_t texture_max_height() override;

    int64_t video_memory_size() override;

    int64_t frame_capacity() override;

    int64_t video_asset_malloc(int64_t size) override;

    int64_t video_asset_write(int64_t addr, rv_texture texture) override;

    int64_t video_asset_free(int64_t addr) override;

    int64_t frame_configure(uint64_t config, rv_color clear_color) override;

    int64_t frame_put(rv_primitive primitive) override;

    int64_t frame_flush() override;
};

}  // namespace rv_3dmppc
