#include <cstdint>
#include <cstring>
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

struct rv_example_cpp_texheader {
    uint16_t version;
    uint16_t format;
    uint16_t width;
    uint16_t height;
    uint16_t palette_count;
};

uint16_t read_u16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

bool parse_texheader(const std::vector<uint8_t> &bytes, rv_example_cpp_texheader &out)
{
    if (bytes.size() < 16) {
        return false;
    }
    if (std::memcmp(bytes.data(), "MPTX", 4) != 0) {
        return false;
    }

    out.version = read_u16(bytes.data() + 4);
    out.format = read_u16(bytes.data() + 6);
    out.width = read_u16(bytes.data() + 8);
    out.height = read_u16(bytes.data() + 10);
    out.palette_count = read_u16(bytes.data() + 12);
    return out.version == 1;
}

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
    void load_sprite();

    rv_pdko *pdk_ = nullptr;
    int64_t screen_width_ = 0;
    int64_t screen_height_ = 0;

    int64_t addr_texels_ = 0;
    int64_t addr_palette_ = 0;

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

void rv_dmain::load_sprite()
{
    std::vector<uint8_t> bytes;
    if (!read_asset("example-sprite.mppctex", bytes)) {
        return;
    }

    rv_example_cpp_texheader header{};
    if (!parse_texheader(bytes, header)) {
        return;
    }

    rv_cv *cv = rv_pdko_cv(pdk_);
    const std::size_t palette_offset = 16;
    const std::size_t palette_bytes = static_cast<std::size_t>(header.palette_count) * 2;
    const std::size_t texel_offset = palette_offset + palette_bytes;
    if (bytes.size() < texel_offset) {
        return;
    }

    const std::size_t texel_bytes = bytes.size() - texel_offset;

    if (header.palette_count > 0) {
        const int64_t addr = rv_cv_video_asset_malloc(cv, static_cast<int64_t>(palette_bytes));
        if (addr < 0) {
            return;
        }

        rv_texture palette = {};
        palette.format = RV_TEXFMT_DIRECT15;
        palette.data = bytes.data() + palette_offset;
        palette.size = palette_bytes;
        palette.width = header.palette_count;
        palette.height = 1;
        if (rv_cv_video_asset_write(cv, addr, &palette) < 0) {
            rv_cv_video_asset_free(cv, addr);
            return;
        }
        addr_palette_ = addr;
    }

    const int64_t addr = rv_cv_video_asset_malloc(cv, static_cast<int64_t>(texel_bytes));
    if (addr < 0) {
        return;
    }

    rv_texture texels = {};
    texels.format = static_cast<rv_texfmt>(header.format);
    texels.data = bytes.data() + texel_offset;
    texels.size = texel_bytes;
    texels.width = header.width;
    texels.height = header.height;
    if (rv_cv_video_asset_write(cv, addr, &texels) < 0) {
        rv_cv_video_asset_free(cv, addr);
        return;
    }
    addr_texels_ = addr;
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

    load_sprite();
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

    if (addr_texels_ != 0) {
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
            sprite.addr_texture = addr_texels_;
            sprite.addr_palette = addr_palette_;
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
    if (!pdk_) {
        return;
    }

    rv_cv *cv = rv_pdko_cv(pdk_);
    if (addr_texels_ != 0) {
        rv_cv_video_asset_free(cv, addr_texels_);
    }
    if (addr_palette_ != 0) {
        rv_cv_video_asset_free(cv, addr_palette_);
    }
    addr_texels_ = 0;
    addr_palette_ = 0;
}

} // namespace example_cpp

RV_MPPC_DISC_ENTRY_DEF(example_cpp::rv_dmain);
