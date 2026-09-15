// rv_dmain: one-time building of the test's textures, font, beep and save.
#include "rv_dmain.hpp"

#include <cmath>

#include "pdk/ca/rv_ca.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/cm/rv_cm.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/cv/rv_texel.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_color/rv_color.hpp"
#include "pdklib/rv_font/rv_font.hpp"
#include "pdklib/rv_textures/rv_texel_pack.hpp"

namespace rv_service
{

namespace
{

// --- the test texture ---

// 16x16 DIRECT15. Small enough to build by hand, big enough that TILE and
// STRETCH look different from CLAMP at a glance.
constexpr int64_t RV_DMAIN_TEX_SIZE = 16;

// Remember the contract's rule: 0000h is a HOLE, not black (opaque black is
// 8000h). The fourth quadrant is deliberately holes so the cut-out path is
// visible, and the border is white so edge behaviour is unmistakable.
constexpr uint16_t RV_DMAIN_TEXEL_RED = rv_pdklib::rv_texel_pack(rv_color5{ 31, 0, 0 });
constexpr uint16_t RV_DMAIN_TEXEL_GREEN = rv_pdklib::rv_texel_pack(rv_color5{ 0, 31, 0 });
constexpr uint16_t RV_DMAIN_TEXEL_BLUE = rv_pdklib::rv_texel_pack(rv_color5{ 0, 0, 31 });
constexpr uint16_t RV_DMAIN_TEXEL_WHITE = rv_pdklib::rv_texel_pack(rv_color5{ 31, 31, 31 });

// --- the beep ---

constexpr int64_t RV_DMAIN_BEEP_RATE = 44100;   // the console's fixed sample rate
constexpr int64_t RV_DMAIN_BEEP_FRAMES = 22050; // half a second
constexpr float RV_DMAIN_BEEP_HZ = 440.0f;
constexpr int16_t RV_DMAIN_BEEP_PEAK = 20000;

constexpr uint32_t RV_DMAIN_SAVE_MAGIC = 0x524D4149; // 'RMAI'

} // namespace

void rv_dmain::build_font()
{
    rv_cv *cv = rv_pdko_cv(pdk_);

    // The atlas is expanded into a buffer the DISC owns and uploaded; the buffer
    // dies at the end of this function because video_asset_write copies during
    // the call. 128x48 IDX4 is 3 KiB - cheap enough to keep resident forever.
    std::vector<uint8_t> atlas(rv_pdklib::rv_font_atlas_size, 0);
    if (!rv_pdklib::rv_font_build_atlas(atlas.data(), atlas.size())) {
        return;
    }

    const int64_t atlas_addr =
        rv_cv_video_asset_malloc(cv, static_cast<int64_t>(rv_pdklib::rv_font_atlas_size));
    if (atlas_addr < 0) {
        return;
    }
    const rv_texture atlas_texture = rv_pdklib::rv_font_atlas_texture(atlas.data());
    if (rv_cv_video_asset_write(cv, atlas_addr, &atlas_texture) < 0) {
        rv_cv_video_asset_free(cv, atlas_addr);
        return;
    }

    // One palette is one colour of text. There is no vertex-colour modulation in
    // the contract yet, so recolouring means uploading another palette and
    // pointing addr_palette at it - which is exactly how the machine this
    // imitates recoloured its fonts.
    std::vector<uint16_t> palette(rv_pdklib::rv_font_palette_entries, 0);
    rv_pdklib::rv_font_build_palette(rv_color{ 220, 226, 240 }, palette.data(), palette.size());

    const int64_t palette_addr =
        rv_cv_video_asset_malloc(cv, static_cast<int64_t>(rv_pdklib::rv_font_palette_size));
    if (palette_addr < 0) {
        rv_cv_video_asset_free(cv, atlas_addr);
        return;
    }
    const rv_texture palette_texture = rv_pdklib::rv_font_palette_texture(palette.data());
    if (rv_cv_video_asset_write(cv, palette_addr, &palette_texture) < 0) {
        rv_cv_video_asset_free(cv, palette_addr);
        rv_cv_video_asset_free(cv, atlas_addr);
        return;
    }

    // The failure colour, over the same atlas. Two palettes, one set of glyphs.
    std::vector<uint16_t> bad(rv_pdklib::rv_font_palette_entries, 0);
    rv_pdklib::rv_font_build_palette(rv_color{ 240, 90, 80 }, bad.data(), bad.size());

    const int64_t bad_addr =
        rv_cv_video_asset_malloc(cv, static_cast<int64_t>(rv_pdklib::rv_font_palette_size));
    const rv_texture bad_texture = rv_pdklib::rv_font_palette_texture(bad.data());
    if (bad_addr >= 0 &&
        rv_cv_video_asset_write(cv, bad_addr, &bad_texture) < 0) {
        rv_cv_video_asset_free(cv, bad_addr);
        addr_font_palette_bad_ = 0;
    } else if (bad_addr >= 0) {
        addr_font_palette_bad_ = bad_addr;
    }

    addr_font_ = atlas_addr;
    addr_font_palette_ = palette_addr;
}

void rv_dmain::build_texture()
{
    rv_cv *cv = rv_pdko_cv(pdk_);

    if (RV_DMAIN_TEX_SIZE > rv_cv_texture_max_width(cv) ||
        RV_DMAIN_TEX_SIZE > rv_cv_texture_max_height(cv)) {
        return; // the machine is smaller than this disc assumed; skip, do not lie
    }

    texels_.assign(static_cast<std::size_t>(RV_DMAIN_TEX_SIZE * RV_DMAIN_TEX_SIZE),
        RV_TEXEL_TRANSPARENT);

    const int64_t half = RV_DMAIN_TEX_SIZE / 2;
    for (int64_t y = 0; y < RV_DMAIN_TEX_SIZE; ++y) {
        for (int64_t x = 0; x < RV_DMAIN_TEX_SIZE; ++x) {
            uint16_t texel;
            if (x < half && y < half) {
                texel = RV_DMAIN_TEXEL_RED;
            } else if (x >= half && y < half) {
                texel = RV_DMAIN_TEXEL_GREEN;
            } else if (x < half && y >= half) {
                texel = RV_DMAIN_TEXEL_BLUE;
            } else {
                texel = RV_TEXEL_TRANSPARENT; // the cut-out quadrant
            }

            const bool border =
                x == 0 || y == 0 || x == RV_DMAIN_TEX_SIZE - 1 || y == RV_DMAIN_TEX_SIZE - 1;
            if (border) {
                texel = RV_DMAIN_TEXEL_WHITE;
            }

            texels_[static_cast<std::size_t>(y * RV_DMAIN_TEX_SIZE + x)] = texel;
        }
    }

    const int64_t bytes = static_cast<int64_t>(texels_.size() * sizeof(uint16_t));
    const int64_t addr = rv_cv_video_asset_malloc(cv, bytes);
    if (addr < 0) {
        return;
    }

    rv_texture texture{};
    texture.format = RV_TEXFMT_DIRECT15;
    texture.data = texels_.data();
    texture.size = static_cast<uint64_t>(bytes);
    texture.width = static_cast<uint64_t>(RV_DMAIN_TEX_SIZE);
    texture.height = static_cast<uint64_t>(RV_DMAIN_TEX_SIZE);

    if (rv_cv_video_asset_write(cv, addr, &texture) < 0) {
        rv_cv_video_asset_free(cv, addr);
        return;
    }
    addr_texture_ = addr;
}

// A second texture in the INDEXED family: 8x8, IDX4, with a palette whose entry
// 0 is 0000h. DIRECT15 alone leaves the palette path untested, and that path is
// where the transparency rule is genuinely non-obvious - the hole is decided
// AFTER the lookup, so an opaque index can become a hole by palette alone.
void rv_dmain::build_idx4_texture()
{
    rv_cv *cv = rv_pdko_cv(pdk_);
    constexpr int64_t size = 8;

    // IDX4 packing. Two texels share a byte, LOW nibble first, and rows
    // are padded to whole bytes - so the stride is (width + 1) / 2, not width/2.
    texels_idx4_.assign(static_cast<std::size_t>(((size + 1) / 2) * size), 0);
    for (int64_t y = 0; y < size; ++y) {
        for (int64_t x = 0; x < size; ++x) {
            // A ring: index 0 (transparent) in the middle, colours around it.
            const bool edge = x == 0 || y == 0 || x == size - 1 || y == size - 1;
            const uint8_t index = edge ? static_cast<uint8_t>(1 + ((x + y) % 3)) : 0;

            const std::size_t byte = static_cast<std::size_t>(y * ((size + 1) / 2) + x / 2);
            if ((x & 1) == 0) {
                texels_idx4_[byte] = static_cast<uint8_t>((texels_idx4_[byte] & 0xF0) | index);
            } else {
                texels_idx4_[byte] =
                    static_cast<uint8_t>((texels_idx4_[byte] & 0x0F) | (index << 4));
            }
        }
    }

    palette_idx4_.assign(16, RV_TEXEL_TRANSPARENT); // entry 0 stays the hole
    palette_idx4_[1] = RV_DMAIN_TEXEL_RED;
    palette_idx4_[2] = RV_DMAIN_TEXEL_GREEN;
    palette_idx4_[3] = RV_DMAIN_TEXEL_WHITE;

    const int64_t texel_addr = rv_cv_video_asset_malloc(cv, static_cast<int64_t>(texels_idx4_.size()));
    if (texel_addr < 0) {
        return;
    }

    rv_texture texels{};
    texels.format = RV_TEXFMT_IDX4;
    texels.data = texels_idx4_.data();
    texels.size = texels_idx4_.size();
    texels.width = static_cast<uint64_t>(size);
    texels.height = static_cast<uint64_t>(size);
    if (rv_cv_video_asset_write(cv, texel_addr, &texels) < 0) {
        rv_cv_video_asset_free(cv, texel_addr);
        return;
    }

    const int64_t palette_addr =
        rv_cv_video_asset_malloc(cv, static_cast<int64_t>(palette_idx4_.size() * sizeof(uint16_t)));
    if (palette_addr < 0) {
        rv_cv_video_asset_free(cv, texel_addr);
        return;
    }

    // A palette is uploaded as a texture of its own: DIRECT15, width = entry
    // count, height = 1. That convention is in rv_texture.hpp, and it is why no
    // separate palette-upload call exists.
    rv_texture palette{};
    palette.format = RV_TEXFMT_DIRECT15;
    palette.data = palette_idx4_.data();
    palette.size = palette_idx4_.size() * sizeof(uint16_t);
    palette.width = palette_idx4_.size();
    palette.height = 1;
    if (rv_cv_video_asset_write(cv, palette_addr, &palette) < 0) {
        rv_cv_video_asset_free(cv, palette_addr);
        rv_cv_video_asset_free(cv, texel_addr);
        return;
    }

    addr_idx4_ = texel_addr;
    addr_idx4_palette_ = palette_addr;
}

void rv_dmain::probe_drive()
{
    rv_cd *cd = rv_pdko_cd(pdk_);
    if (!cd) {
        return;
    }

    // A name carrying path separators must be refused before anything touches
    // the medium - it is an attempt to leave the disc, not a spelling mistake.
    // This probe asserts the drive answers INVAL rather than merely NOENT.
    drive_rejects_paths_ = rv_cd_asset_open(cd, "../../etc/passwd") == RV_ERR_INVAL &&
        rv_cd_asset_open(cd, "assets/thing.obj") == RV_ERR_INVAL;

    const int64_t handle = rv_cd_asset_open(cd, "protagonist.obj");
    if (handle < 0) {
        return; // no medium inserted is a legal, quiet outcome
    }

    const int64_t size = rv_cd_asset_size(cd, handle);
    if (size <= 0) {
        return;
    }

    std::vector<uint8_t> buffer(static_cast<std::size_t>(size));
    // AUTHORITATIVE is what asset_read returns, not what asset_size promised:
    // the contract calls the size a hint that may go stale between the calls.
    const int64_t read = rv_cd_asset_read(cd, handle, buffer.data(), size);
    if (read < 0) {
        return;
    }

    asset_bytes_ = read;
    asset_ok_ = read > 0 && buffer[0] == '#'; // a Wavefront .obj opens with a comment

    // The same name must resolve to the same handle, forever.
    if (rv_cd_asset_open(cd, "protagonist.obj") != handle) {
        asset_ok_ = false;
    }
}

void rv_dmain::load_save()
{
    rv_cm *cm = rv_pdko_cm(pdk_);
    if (!cm) {
        return;
    }

    if (rv_cm_card_slot_size(cm) < static_cast<int64_t>(sizeof(rv_dmain_save))) {
        return; // this machine's slots cannot hold our blob
    }

    rv_dmain_save blob{ RV_DMAIN_SAVE_MAGIC, 0 };

    const int64_t size = rv_cm_card_size(cm, 0);
    if (size >= 0) {
        rv_dmain_save stored{};
        if (rv_cm_card_read(cm, 0, &stored, static_cast<int64_t>(sizeof(stored))) >= 0 &&
            stored.magic == RV_DMAIN_SAVE_MAGIC) {
            blob.boot_count = stored.boot_count;
        }
    } else if (size != RV_ERR_NOENT) {
        // A real medium failure. NOT the same as "no save yet": overwriting now
        // would destroy a save that may well be there, so play without one.
        return;
    }

    ++blob.boot_count;
    if (rv_cm_card_write(cm, 0, &blob, static_cast<int64_t>(sizeof(blob))) == RV_OK) {
        boot_count_ = blob.boot_count;
        card_ok_ = true;
    }
}

void rv_dmain::build_beep()
{
    rv_ca *ca = rv_pdko_ca(pdk_);
    if (!ca || rv_ca_voice_count(ca) < 1) {
        return;
    }

    // An exponentially decaying sine. The console has no pitch control,
    // so a sample plays at exactly the rate it was recorded for - the frequency
    // is baked in here, against RV_DMAIN_BEEP_RATE, and nothing downstream can
    // bend it. The decay lives in the SAMPLE rather than the envelope so the
    // beep stays recognisable even with an ADSR nobody has tuned.
    std::vector<int16_t> pcm(static_cast<std::size_t>(RV_DMAIN_BEEP_FRAMES));
    for (int64_t i = 0; i < RV_DMAIN_BEEP_FRAMES; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(RV_DMAIN_BEEP_RATE);
        const float envelope = std::exp(-6.0f * t);
        const float wave = std::sin(6.28318531f * RV_DMAIN_BEEP_HZ * t);
        pcm[static_cast<std::size_t>(i)] =
            static_cast<int16_t>(wave * envelope * static_cast<float>(RV_DMAIN_BEEP_PEAK));
    }

    const int64_t bytes = static_cast<int64_t>(pcm.size() * sizeof(int16_t));
    const int64_t addr = rv_ca_sound_asset_malloc(ca, bytes);
    if (addr < 0) {
        return;
    }

    rv_sample sample{};
    sample.data = pcm.data();
    sample.size = bytes;
    if (rv_ca_sound_asset_write(ca, addr, &sample) < 0) {
        rv_ca_sound_asset_free(ca, addr);
        return;
    }

    rv_voice_conf conf{};
    conf.voice = 1; // voice 0
    conf.loop_type = RV_LOOP_NONE;
    conf.sample_address = addr;
    conf.ar = 5;
    conf.dr = 40;
    conf.sr = 0;
    conf.rr = 120;
    conf.sl = 26000;
    conf.volume = 32767;
    conf.volume_l = 32767;
    conf.volume_r = 32767;
    if (rv_ca_voice_setup(ca, &conf) < 0) {
        return;
    }

    addr_beep_ = addr;
    beep_ok_ = true;
}

} // namespace rv_service
