#include <cstdint>
#include <vector>

#include "pdk/cd/rv_cd.h"
#include "pdk/cio/rv_cio.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/de/rv_de.h"
#include "pdk/rv_err.h"
#include "pdk/de/rv_dv.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_cppdisc/rv_cppdisc.hpp"

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

// rv_dmain_base_ carries everything pdklib/rv_cppdisc/rv_cppdisc.hpp
// considers the same for any C++ disc: the startup guards, MENU-button
// press-edge tracking and read_asset(). rv_dmain adds exactly what makes
// this disc THIS game - the sprite grid, the text bar and their layout.
RV_MPPC_DISC_CPP_DEF(rv_dmain_base_)

class rv_dmain : public rv_dmain_base_
{
public:
    int64_t disc_initialize(rv_pdko *pdk)
    {
        const int64_t base_result = rv_dmain_base_::disc_initialize(pdk);
        if (base_result < 0) {
            return base_result;
        }

        std::vector<uint8_t> text;
        if (read_asset("example-text.txt", text)) {
            text_bytes_ = static_cast<int64_t>(text.size());
        }

        return RV_OK;
    }

    void frame_update(float dt)
    {
        rv_dmain_base_::frame_update(dt);
        if (dt > 0.0f) {
            phase_ += dt;
        }
    }

    void frame_render();

    void disc_shutdown()
    {
        // Nothing to release: this disc never acquired the texture it drew, only
        // named it, and the drive frees everything it made resident on its own
        // when this disc unloads.
    }

    const char *disc_title() const
    {
        return "example-cpp";
    }

private:
    int64_t text_bytes_ = 0;
    float phase_ = 0.0f;
};

void rv_dmain::frame_render()
{
    frame_begin(rv_color{ 20, 24, 40 });

    int64_t addr_texture = 0;
    int64_t addr_palette = 0;
    if (texture_resident(RV_EXAMPLE_CPP_TEXTURE_NAME, addr_texture, addr_palette)) {
        const rv_texture_mapping_type modes[3] = {
            RV_TEXWRAP_CLAMP, RV_TEXWRAP_TILE, RV_TEXWRAP_STRETCH
        };
        const float size = 48.0f;
        const float gap = 12.0f;
        const float total = 3.0f * size + 2.0f * gap;
        const float x0 = (static_cast<float>(screen_width_) - total) * 0.5f;
        const float y0 = (static_cast<float>(screen_height_) - size) * 0.5f;

        for (int i = 0; i < 3; ++i) {
            rv_sprite sprite = {};
            sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE;
            sprite.addr_texture = addr_texture;
            sprite.addr_palette = addr_palette;
            sprite.color = rv_color{ 255, 255, 255 };
            sprite.mapping = modes[i];
            sprite.x = static_cast<int16_t>(x0 + static_cast<float>(i) * (size + gap));
            sprite.y = static_cast<int16_t>(y0);
            sprite.width = static_cast<uint16_t>(size);
            sprite.height = static_cast<uint16_t>(size);

            draw_sprite(sprite, RV_EXAMPLE_CPP_DEPTH_SPRITE);
        }
    }

    if (text_bytes_ > 0) {
        rv_sprite sprite = {};
        sprite.fill_mode = RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
        sprite.addr_texture = 0;
        sprite.addr_palette = 0;
        sprite.color = rv_color{ 90, 210, 220 };
        sprite.mapping = RV_TEXWRAP_CLAMP;
        sprite.x = 8;
        sprite.y = static_cast<int16_t>(screen_height_ - 16);
        sprite.width = static_cast<uint16_t>(text_bytes_ * 4);
        sprite.height = 8;

        draw_sprite(sprite, RV_EXAMPLE_CPP_DEPTH_BAR);
    }

    frame_end();
}

} // namespace example_cpp

RV_MPPC_DISC_ENTRY_DEF(example_cpp::rv_dmain);
