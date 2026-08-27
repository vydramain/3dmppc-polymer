#include <cstdint>
#include <cstring>
#include <vector>

#include "pdk/cd/rv_cd.hpp"
#include "pdk/cio/rv_cio.hpp"
#include "pdk/cv/rv_cv.hpp"
#include "pdk/de/rv_de.hpp"
#include "pdk/rv_err.hpp"
#include "pdk/de/rv_dv.hpp"

namespace example_cpp
{

namespace
{

constexpr int32_t RV_EXAMPLE_CPP_DEPTH_BADGE = 0;
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

class rv_dmain : public rv_pdk::rv_de
{
public:
    int64_t disc_initialize(rv_pdk::rv_pdko &pdk) override;
    void frame_update(float dt) override;
    void frame_render() override;
    bool disc_release() const override
    {
        return release_;
    }
    void disc_shutdown() override;
    const char *disc_title() const override
    {
        return "example-cpp";
    }

private:
    bool read_asset(const char *name, std::vector<uint8_t> &out);
    void load_badge();

    rv_pdk::rv_pdko *pdk_ = nullptr;
    int64_t screen_width_ = 0;
    int64_t screen_height_ = 0;

    int64_t addr_texels_ = 0;
    int64_t addr_palette_ = 0;

    int64_t greeting_bytes_ = 0;
    float phase_ = 0.0f;
    uint64_t prev_buttons_ = 0;
    bool release_ = false;
};

bool rv_dmain::read_asset(const char *name, std::vector<uint8_t> &out)
{
    rv_pdk::rv_cd *cd = pdk_->cd();
    if (!cd) {
        return false;
    }

    const int64_t handle = cd->asset_open(name);
    if (handle < 0) {
        return false;
    }

    const int64_t size = cd->asset_size(handle);
    if (size < 0) {
        return false;
    }

    out.assign(static_cast<std::size_t>(size), 0);
    const int64_t read = cd->asset_read(handle, out.data(), size);
    if (read < 0) {
        return false;
    }

    out.resize(static_cast<std::size_t>(read));
    return true;
}

void rv_dmain::load_badge()
{
    std::vector<uint8_t> bytes;
    if (!read_asset("badge.mppctex", bytes)) {
        return;
    }

    rv_example_cpp_texheader header{};
    if (!parse_texheader(bytes, header)) {
        return;
    }

    rv_pdk::rv_cv *cv = pdk_->cv();
    const std::size_t palette_offset = 16;
    const std::size_t palette_bytes = static_cast<std::size_t>(header.palette_count) * 2;
    const std::size_t texel_offset = palette_offset + palette_bytes;
    if (bytes.size() < texel_offset) {
        return;
    }

    const std::size_t texel_bytes = bytes.size() - texel_offset;

    if (header.palette_count > 0) {
        const int64_t addr = cv->video_asset_malloc(static_cast<int64_t>(palette_bytes));
        if (addr < 0) {
            return;
        }

        rv_pdk::rv_texture palette{};
        palette.format = rv_pdk::RV_TEXFMT_DIRECT15;
        palette.data = bytes.data() + palette_offset;
        palette.size = palette_bytes;
        palette.width = header.palette_count;
        palette.height = 1;
        if (cv->video_asset_write(addr, &palette) < 0) {
            cv->video_asset_free(addr);
            return;
        }
        addr_palette_ = addr;
    }

    const int64_t addr = cv->video_asset_malloc(static_cast<int64_t>(texel_bytes));
    if (addr < 0) {
        return;
    }

    rv_pdk::rv_texture texels{};
    texels.format = static_cast<rv_pdk::rv_texfmt>(header.format);
    texels.data = bytes.data() + texel_offset;
    texels.size = texel_bytes;
    texels.width = header.width;
    texels.height = header.height;
    if (cv->video_asset_write(addr, &texels) < 0) {
        cv->video_asset_free(addr);
        return;
    }
    addr_texels_ = addr;
}

int64_t rv_dmain::disc_initialize(rv_pdk::rv_pdko &pdk)
{
    pdk_ = &pdk;

    rv_pdk::rv_cv *cv = pdk_->cv();
    rv_pdk::rv_cio *cio = pdk_->cio();
    if (!cv || !cio) {
        return rv_pdk::RV_ERR_INVAL;
    }

    screen_width_ = cv->screen_width();
    screen_height_ = cv->screen_height();

    if (screen_width_ < 64 || screen_height_ < 64) {
        return rv_pdk::RV_ERR_INVAL;
    }
    if (cv->frame_capacity() < 8) {
        return rv_pdk::RV_ERR_INVAL;
    }
    if (cio->iport_count() < 1) {
        return rv_pdk::RV_ERR_INVAL;
    }

    std::vector<uint8_t> greeting;
    if (read_asset("greeting.txt", greeting)) {
        greeting_bytes_ = static_cast<int64_t>(greeting.size());
    }

    load_badge();
    return rv_pdk::RV_OK;
}

void rv_dmain::frame_update(float dt)
{
    if (dt > 0.0f) {
        phase_ += dt;
    }

    const uint64_t now = pdk_->cio()->iport_state(0).buttons;
    if ((now & ~prev_buttons_) & rv_pdk::RV_ISOURCE_MENU_BTTN_MENU) {
        release_ = true;
    }
    prev_buttons_ = now;
}

void rv_dmain::frame_render()
{
    rv_pdk::rv_cv *cv = pdk_->cv();

    cv->frame_configure(0, rv_pdk::rv_color{ 20, 24, 40 });

    if (addr_texels_ != 0) {
        const rv_pdk::rv_texture_mapping_type modes[3] = {
            rv_pdk::RV_TEXWRAP_CLAMP, rv_pdk::RV_TEXWRAP_TILE, rv_pdk::RV_TEXWRAP_STRETCH
        };
        const float size = 48.0f;
        const float gap = 12.0f;
        const float total = 3.0f * size + 2.0f * gap;
        const float x0 = (static_cast<float>(screen_width_) - total) * 0.5f;
        const float y0 = (static_cast<float>(screen_height_) - size) * 0.5f;

        for (int i = 0; i < 3; ++i) {
            rv_pdk::rv_primitive primitive{};
            primitive.type = rv_pdk::RV_PRIMITIVE_SPRITE;
            primitive.depth = RV_EXAMPLE_CPP_DEPTH_BADGE;

            rv_pdk::rv_sprite &sprite = primitive.data.sprite;
            sprite.fill_mode = rv_pdk::RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE;
            sprite.addr_texture = addr_texels_;
            sprite.addr_palette = addr_palette_;
            sprite.color = rv_pdk::rv_color{ 255, 255, 255 };
            sprite.mapping = modes[i];
            sprite.x = static_cast<int16_t>(x0 + static_cast<float>(i) * (size + gap));
            sprite.y = static_cast<int16_t>(y0);
            sprite.width = static_cast<uint16_t>(size);
            sprite.height = static_cast<uint16_t>(size);

            cv->frame_put(&primitive);
        }
    }

    if (greeting_bytes_ > 0) {
        rv_pdk::rv_primitive primitive{};
        primitive.type = rv_pdk::RV_PRIMITIVE_SPRITE;
        primitive.depth = RV_EXAMPLE_CPP_DEPTH_BAR;

        rv_pdk::rv_sprite &sprite = primitive.data.sprite;
        sprite.fill_mode = rv_pdk::RV_PRIMITIVE_FILL_MODE_FLAT_COLOURED;
        sprite.addr_texture = 0;
        sprite.addr_palette = 0;
        sprite.color = rv_pdk::rv_color{ 90, 210, 220 };
        sprite.mapping = rv_pdk::RV_TEXWRAP_CLAMP;
        sprite.x = 8;
        sprite.y = static_cast<int16_t>(screen_height_ - 16);
        sprite.width = static_cast<uint16_t>(greeting_bytes_ * 4);
        sprite.height = 8;

        cv->frame_put(&primitive);
    }

    cv->frame_flush();
}

void rv_dmain::disc_shutdown()
{
    if (!pdk_) {
        return;
    }

    rv_pdk::rv_cv *cv = pdk_->cv();
    if (addr_texels_ != 0) {
        cv->video_asset_free(addr_texels_);
    }
    if (addr_palette_ != 0) {
        cv->video_asset_free(addr_palette_);
    }
    addr_texels_ = 0;
    addr_palette_ = 0;
}

} // namespace example_cpp

RV_MPPC_DISC_ENTRY_DEF(example_cpp::rv_dmain);
