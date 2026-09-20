#include <cstdint>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdk/cio/rv_cio.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/de/rv_de.h"
#include "pdk/rv_err.h"
#include "pdk/de/rv_dv.h"
#include "pdk/rv_pdko.h"

namespace example_cpp
{

namespace
{

constexpr int32_t RV_EXAMPLE_CPP_DEPTH_SPRITE = 0;
constexpr int32_t RV_EXAMPLE_CPP_DEPTH_BAR = 400;

// The one texture this disc draws. Never acquired or released, only ever
// named - the drive makes it resident the first time frame_render asks for
// this name, and frees it itself when this disc unloads.
constexpr const char *RV_EXAMPLE_CPP_TEXTURE_NAME = "example-sprite.mppctex";

} // namespace

class rv_dmain
{
public:
    int64_t disc_initialize(rv_pdko *pdk);
    void frame_update(float dt);
    void frame_render();
    int disc_release() const
    {
        return release_;
    }
    void disc_shutdown();
    const char *disc_title() const
    {
        return "example-cpp";
    }

private:
    bool read_asset(const char *name, std::vector<uint8_t> &out);

    rv_pdko *pdk_ = nullptr;
    int64_t screen_width_ = 0;
    int64_t screen_height_ = 0;

    int64_t text_bytes_ = 0;
    float phase_ = 0.0f;
    uint64_t prev_buttons_ = 0;
    bool release_ = false;
};

bool rv_dmain::read_asset(const char *name, std::vector<uint8_t> &out)
{
    rv_cd *cd = rv_pdko_cd(pdk_);
    if (!cd) {
        return false;
    }

    const int64_t handle = rv_cd_asset_open(cd, name);
    if (handle < 0) {
        return false;
    }

    const int64_t size = rv_cd_asset_size(cd, handle);
    if (size < 0) {
        return false;
    }

    out.assign(static_cast<std::size_t>(size), 0);
    const int64_t read = rv_cd_asset_read(cd, handle, out.data(), size);
    if (read < 0) {
        return false;
    }

    out.resize(static_cast<std::size_t>(read));
    return true;
}

int64_t rv_dmain::disc_initialize(rv_pdko *pdk)
{
    pdk_ = pdk;

    rv_cv *cv = rv_pdko_cv(pdk_);
    rv_cio *cio = rv_pdko_cio(pdk_);
    if (!cv || !cio) {
        return RV_ERR_INVAL;
    }

    screen_width_ = rv_cv_screen_width(cv);
    screen_height_ = rv_cv_screen_height(cv);

    if (screen_width_ < 64 || screen_height_ < 64) {
        return RV_ERR_INVAL;
    }
    if (rv_cv_frame_capacity(cv) < 8) {
        return RV_ERR_INVAL;
    }
    if (rv_cio_iport_count(cio) < 1) {
        return RV_ERR_INVAL;
    }

    std::vector<uint8_t> text;
    if (read_asset("example-text.txt", text)) {
        text_bytes_ = static_cast<int64_t>(text.size());
    }

    return RV_OK;
}

void rv_dmain::frame_update(float dt)
{
    if (dt > 0.0f) {
        phase_ += dt;
    }

    const uint64_t now = rv_cio_iport_state(rv_pdko_cio(pdk_), 0).buttons;
    if ((now & ~prev_buttons_) & RV_ISOURCE_MENU_BTTN_MENU) {
        release_ = true;
    }
    prev_buttons_ = now;
}

void rv_dmain::frame_render()
{
    rv_cv *cv = rv_pdko_cv(pdk_);

    rv_cv_frame_configure(cv, 0, rv_color{ 20, 24, 40 });

    rv_cd *cd = rv_pdko_cd(pdk_);
    const int64_t addr_texture = rv_cd_resource_addr(cd, RV_CD_RESOURCE_TEXTURE, RV_EXAMPLE_CPP_TEXTURE_NAME);
    if (addr_texture >= 0) {
        const int64_t addr_palette =
            rv_cd_resource_palette_addr(cd, RV_CD_RESOURCE_TEXTURE, RV_EXAMPLE_CPP_TEXTURE_NAME);
        const rv_texture_mapping_type modes[3] = {
            RV_TEXWRAP_CLAMP, RV_TEXWRAP_TILE, RV_TEXWRAP_STRETCH
        };
        const float size = 48.0f;
        const float gap = 12.0f;
        const float total = 3.0f * size + 2.0f * gap;
        const float x0 = (static_cast<float>(screen_width_) - total) * 0.5f;
        const float y0 = (static_cast<float>(screen_height_) - size) * 0.5f;

        for (int i = 0; i < 3; ++i) {
            rv_primitive primitive = {};
            primitive.type = RV_PRIMITIVE_SPRITE;
            primitive.depth = RV_EXAMPLE_CPP_DEPTH_SPRITE;

            rv_sprite &sprite = primitive.data.sprite;
            sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE;
            sprite.addr_texture = addr_texture;
            sprite.addr_palette = addr_palette;
            sprite.color = rv_color{ 255, 255, 255 };
            sprite.mapping = modes[i];
            sprite.x = static_cast<int16_t>(x0 + static_cast<float>(i) * (size + gap));
            sprite.y = static_cast<int16_t>(y0);
            sprite.width = static_cast<uint16_t>(size);
            sprite.height = static_cast<uint16_t>(size);

            rv_cv_frame_put(cv, &primitive);
        }
    }

    if (text_bytes_ > 0) {
        rv_primitive primitive = {};
        primitive.type = RV_PRIMITIVE_SPRITE;
        primitive.depth = RV_EXAMPLE_CPP_DEPTH_BAR;

        rv_sprite &sprite = primitive.data.sprite;
        sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
        sprite.addr_texture = 0;
        sprite.addr_palette = 0;
        sprite.color = rv_color{ 90, 210, 220 };
        sprite.mapping = RV_TEXWRAP_CLAMP;
        sprite.x = 8;
        sprite.y = static_cast<int16_t>(screen_height_ - 16);
        sprite.width = static_cast<uint16_t>(text_bytes_ * 4);
        sprite.height = 8;

        rv_cv_frame_put(cv, &primitive);
    }

    rv_cv_frame_flush(cv);
}

void rv_dmain::disc_shutdown()
{
    // Nothing to release: this disc never acquired the texture it drew, only
    // named it, and the drive frees everything it made resident on its own
    // when this disc unloads.
}

} // namespace example_cpp

RV_MPPC_DISC_ENTRY_DEF(example_cpp::rv_dmain);
