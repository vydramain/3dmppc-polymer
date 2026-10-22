#include "example-cpp-bare.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "example-cpp-bare-layout.hpp"
#include "pdk/cg/rv_cg.h"
#include "pdk/cv/rv_cv.h"
#include "pdk/de/rv_dv.h"
#include "pdk/rv_err.h"
#include "pdk/rv_pdko.h"
#include "pdklib/rv_cppdisc/rv_cppdisc.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"

namespace example_cpp_bare
{

namespace
{

// The frame's clear colour: the same dark blue as example-cpp.
constexpr rv_color RV_EXAMPLE_CPP_BARE_BACKGROUND = { 20, 24, 40 };

} // namespace

// The base supplies disc_initialize, frame_update and disc_release.
RV_MPPC_DISC_CPP_DEF(rv_dmain_base_)

class rv_dmain : public rv_dmain_base_
{
public:
    int64_t disc_initialize(rv_pdko *pdk)
    {
        const int64_t base_result = rv_dmain_base_::disc_initialize(pdk);
        if (base_result != RV_OK) {
            return base_result;
        }
        // No route: the disc runs without it and says why; nothing is drawn and no sound plays.
        const int route_code = rv_route_keys(keys_);
        if (route_code != RV_OK) {
            RV_LOG_ERR("scene", "route not used: rv_err {}", route_code);
            return RV_OK;
        }
        route_read_ = true;
        rv_lamps_schedule(keys_, lamp_schedule_);
        init_sound();
        return RV_OK;
    }

    void frame_update(float dt)
    {
        rv_dmain_base_::frame_update(dt);
        const int64_t tick = dt > 0.0f ? 1 : 0;
        frame_ += tick;
    }

    void frame_render()
    {
        frame_begin(RV_EXAMPLE_CPP_BARE_BACKGROUND);
        play_sound();
        draw_world();
        frame_end();
    }

    void disc_shutdown()
    {
        // Nothing to release: the drive frees the disc's assets when it unloads.
    }

    const char *disc_title() const
    {
        return "example-cpp-bare";
    }

private:
    void init_sound();
    void play_sound();
    void log_sound_error(int64_t code);
    void draw_world();
    void draw_entry(const char *model, const rv_pdklib::rv_gte_matrix &world, rv_color tint, bool decal);
    void log_draw_error(int code);

    // The route keys of the layout, sorted by index, and the lamps' burn windows in bare_lamps() order.
    std::vector<rv_route_key> keys_;
    std::vector<rv_lamp_schedule> lamp_schedule_;
    // rv_route_keys succeeded: the camera and the lamps are known, so the disc draws.
    bool route_read_ = false;
    // One flag for the first draw failure, the camera's or a put's, as in pdkkit: a repeat of the failure stays silent.
    bool draw_error_logged_ = false;
    // The frame clock: the number of updates with dt > 0, as pdkkit's.
    int64_t frame_ = 0;
    // The music and the clack; sound_ready_ is false when init_sound failed, and then nothing plays.
    rv_sound_state sound_state_;
    bool sound_ready_ = false;
    // Assets whose failure is logged already: one log per asset, so a later failure of another asset still shows.
    std::vector<std::string> sound_errors_logged_;
};

// Loads the music and tabulates the areas; runs once, after rv_route_keys succeeded.
void rv_dmain::init_sound()
{
    const int64_t sound_code = rv_sound_init(pdk_, keys_, sound_state_);
    if (sound_code < 0) {
        RV_LOG_ERR("sound", "sound off: {} rv_err {}", sound_state_.error_asset, sound_code);
        return;
    }
    sound_ready_ = true;
}

// Plays the frame's sound; without init_sound there is no music, and init_sound has logged why.
void rv_dmain::play_sound()
{
    if (!sound_ready_) {
        return;
    }
    // Replaces pdkkit's board_found_: the board's decals are in the table exactly when the board is, so they say
    // whether there is a board to clack.
    const bool board_present = !bare_decals().empty();
    const int64_t sound_code = rv_sound_update(pdk_, frame_ % RV_ROUTE_PERIOD_FRAMES, board_present, sound_state_);
    if (sound_code < 0) {
        log_sound_error(sound_code);
    }
}

// Logs the first failure of each asset only: the same failure would repeat in every frame.
void rv_dmain::log_sound_error(int64_t code)
{
    const std::string asset = sound_state_.error_asset;
    if (std::find(sound_errors_logged_.begin(), sound_errors_logged_.end(), asset) != sound_errors_logged_.end()) {
        return;
    }
    sound_errors_logged_.push_back(asset);
    RV_LOG_ERR("sound", "sound not played: {} rv_err {}", asset, code);
}

// Sets the camera of the frame, then puts the meshes, the lamps lit in this frame, the decals of the lit cells, in
// that order. The camera is set first, so a failure draws nothing; the puts go on after a failed one.
void rv_dmain::draw_world()
{
    if (!route_read_) {
        return;
    }
    rv_cg_camera camera = {};
    const int64_t screen_height = rv_cv_screen_height(rv_pdko_cv(pdk_));
    const int camera_code = rv_route_camera_at(keys_, frame_, screen_height, camera);
    if (camera_code != RV_OK) {
        log_draw_error(camera_code);
        return;
    }
    const int set_code = rv_draw_camera(pdk_, camera);
    if (set_code != RV_OK) {
        log_draw_error(set_code);
        return;
    }
    for (const bare_mesh &mesh : bare_meshes()) {
        draw_entry(mesh.model, mesh.world, mesh.tint, false);
    }
    const std::span<const bare_lamp> lamps = bare_lamps();
    for (std::size_t i = 0; i < lamps.size(); ++i) {
        if (rv_lamp_lit(lamp_schedule_[i], frame_)) {
            draw_entry(lamps[i].model, lamps[i].world, lamps[i].tint, false);
        }
    }
    // rv_board_cell_lit does not take the period's modulo itself, so the phase is passed in.
    const int64_t phase = frame_ % RV_ROUTE_PERIOD_FRAMES;
    for (const bare_decal &decal : bare_decals()) {
        if (rv_board_cell_lit(decal.cell, phase)) {
            draw_entry(decal.model, decal.world, decal.tint, true);
        }
    }
}

void rv_dmain::draw_entry(const char *model, const rv_pdklib::rv_gte_matrix &world, rv_color tint, bool decal)
{
    const int put_code = rv_draw_put(pdk_, model, world, tint, decal);
    if (put_code != RV_OK) {
        log_draw_error(put_code);
    }
}

// Logs the first failure only: the same failure would repeat in every frame.
void rv_dmain::log_draw_error(int code)
{
    if (draw_error_logged_) {
        return;
    }
    draw_error_logged_ = true;
    RV_LOG_ERR("draw", "world not drawn: rv_err {}", code);
}

} // namespace example_cpp_bare

RV_MPPC_DISC_ENTRY_DEF(example_cpp_bare::rv_dmain);
