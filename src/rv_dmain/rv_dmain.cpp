// rv_dmain: the rv_de hooks - initialize, update, render, shutdown.
#include "rv_dmain.hpp"

#include <cmath>

#include "pdk/ca/rv_ca.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/cio/rv_cio.h"
#include "pdk/cm/rv_cm.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_color/rv_color.hpp"

namespace rv_service
{

namespace
{

// --- baked assumptions, validated against the machine in disc_initialize ---

constexpr int64_t RV_DMAIN_MIN_SCREEN = 64;
constexpr int64_t RV_DMAIN_MIN_FRAME_CAPACITY = 16;
constexpr int64_t RV_DMAIN_MIN_PORTS = 1;

// --- the show ---

constexpr float RV_DMAIN_HUE_RATE = 0.05f;
constexpr float RV_DMAIN_SPIN_RATE = 0.7f;

} // namespace

// --- initialization ----------------------------------------------------------

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
    frame_capacity_ = rv_cv_frame_capacity(cv);
    iport_count_ = rv_cio_iport_count(cio);
    video_memory_size_ = rv_cv_video_memory_size(cv);

    rv_ca *ca = rv_pdko_ca(pdk_);
    if (ca) {
        voice_count_ = rv_ca_voice_count(ca);
        sound_memory_size_ = rv_ca_sound_memory_size(ca);
    }

    rv_cm *cm = rv_pdko_cm(pdk_);
    if (cm) {
        card_slots_ = rv_cm_card_slots(cm);
        card_slot_size_ = rv_cm_card_slot_size(cm);
    }

    // A populated slot advertises what it can report; an empty one answers 0.
    for (int64_t port = 0; port < iport_count_; ++port) {
        if (rv_cio_iport_abilities(cio, port) != 0) {
            ++pads_connected_;
        }
    }

    // The point of this hook: the disc bakes assumptions, the machine answers,
    // and a mismatch is refused HERE rather than discovered as garbage on
    // screen half a second in. The console reports the negative code and stops.
    if (screen_width_ < RV_DMAIN_MIN_SCREEN || screen_height_ < RV_DMAIN_MIN_SCREEN) {
        return RV_ERR_INVAL;
    }
    if (frame_capacity_ < RV_DMAIN_MIN_FRAME_CAPACITY) {
        return RV_ERR_INVAL;
    }
    if (iport_count_ < RV_DMAIN_MIN_PORTS) {
        return RV_ERR_INVAL;
    }

    // One previous-mask slot per port, allocated once: frame_update must not
    // allocate, and the port count is session-stable.
    prev_buttons_.assign(static_cast<std::size_t>(iport_count_), 0ULL);

    build_texture();
    build_idx4_texture();
    build_font();
    probe_drive();
    load_save();
    build_beep();

    return RV_OK;
}

// --- simulation --------------------------------------------------------------

void rv_dmain::frame_update(float dt)
{
    if (!pdk_) {
        return;
    }
    if (dt < 0.0f) {
        dt = 0.0f; // a non-monotonic clock must not run the show backwards
    }

    hue_ += dt * RV_DMAIN_HUE_RATE;
    hue_ -= std::floor(hue_);
    spin_ += dt * RV_DMAIN_SPIN_RATE;

    rv_cio *cio = rv_pdko_cio(pdk_);
    rv_ca *ca = rv_pdko_ca(pdk_);

    // THEOREM: edge detection by snapshot diff. rv_cio reports the CURRENT level
    // of every source and never a press/release event, so "was it pressed THIS
    // frame" is (now & ~was) — set now, clear a frame ago. Acting on the level
    // would fire the beep sixty times a second for as long as the key is held;
    // only the rising edge is an intent.
    for (int64_t port = 0; port < iport_count_; ++port) {
        const uint64_t now = rv_cio_iport_state(cio, port).buttons;
        uint64_t &was = prev_buttons_[static_cast<std::size_t>(port)];
        const uint64_t pressed = now & ~was;
        was = now;

        // Esc on the keyboard, Option/Start on a pad — any port may ask.
        if (pressed & RV_ISOURCE_MENU_BTTN_MENU) {
            release_ = true;
        }

        if ((pressed & RV_ISOURCE_FRONT_BTTN_SOUTH) && beep_ok_ && ca) {
            rv_ca_voice_play(ca, 1);
        }
    }
}

// --- the frame ---------------------------------------------------------------

void rv_dmain::frame_render()
{
    if (!pdk_) {
        return;
    }

    // No simulation lives here: frame_render() is now always called, and with
    // cv null the calls it makes land on a no-op, so the show must advance
    // identically either way (rv_de::frame_render).
    rv_cv *cv = rv_pdko_cv(pdk_);

    // The Z flag is on because the cut-out row depends on it: a hole must write
    // neither colour nor depth, and that is only observable when per-pixel depth
    // resolves primitives sharing an ordering-table bucket.
    rv_cv_frame_configure(cv, RV_PIPELINE_BUFFER_CONFIG_TYPE_Z,
        rv_pdklib::rv_hsv_to_rgb(hue_, 0.5f, 0.35f));

    draw_test_grid();
    draw_post();

    // The console does NOT flush for the disc (rv_de::frame_render). Nothing
    // filed above reaches the screen without this call.
    rv_cv_frame_flush(cv);
}

void rv_dmain::disc_shutdown()
{
    // The facade is still valid here and this is the LAST moment it is. Whatever
    // disc_initialize took is given back now, not in a destructor: once this
    // returns the console may unload the disc's code, and a destructor belonging
    // to unmapped code cannot run.
    if (!pdk_) {
        return;
    }

    rv_cv *cv = rv_pdko_cv(pdk_);
    if (addr_texture_ != 0) {
        rv_cv_video_asset_free(cv, addr_texture_);
    }
    if (addr_font_ != 0) {
        rv_cv_video_asset_free(cv, addr_font_);
    }
    if (addr_font_palette_ != 0) {
        rv_cv_video_asset_free(cv, addr_font_palette_);
    }
    if (addr_font_palette_bad_ != 0) {
        rv_cv_video_asset_free(cv, addr_font_palette_bad_);
    }
    if (addr_idx4_ != 0) {
        rv_cv_video_asset_free(cv, addr_idx4_);
    }
    if (addr_idx4_palette_ != 0) {
        rv_cv_video_asset_free(cv, addr_idx4_palette_);
    }
    addr_texture_ = 0;
    addr_font_ = 0;
    addr_font_palette_ = 0;
    addr_font_palette_bad_ = 0;
    addr_idx4_ = 0;
    addr_idx4_palette_ = 0;

    rv_ca *ca = rv_pdko_ca(pdk_);
    if (ca && addr_beep_ != 0) {
        // Stop first: the contract makes sound_asset_free() answer RV_ERR_BUSY
        // while a voice is still reading out of the region.
        rv_ca_voice_stop(ca, 1);
        rv_ca_sound_asset_free(ca, addr_beep_);
        addr_beep_ = 0;
    }
}

const char *rv_dmain::disc_title() const
{
    return "rv_dmain - service test";
}

} // namespace rv_service
