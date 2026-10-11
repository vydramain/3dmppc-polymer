#include "example-cpp-sound.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_loop.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "example-cpp-board.hpp"
#include "example-cpp-route.hpp"

namespace example_cpp
{

namespace
{

// Eye x of the camera, metres, as scenes/main.scene.toml places the objects: factory and street
// start at x = 0, apartment at x = 30. Eye x < 0 is the workshop, x > 30 the flat, between them the street.
constexpr float RV_SOUND_WORKSHOP_MAX_X = 0.0f;
constexpr float RV_SOUND_FLAT_MIN_X = 30.0f;

// The music loop of each rv_sound_area, by its value; the samples are the disc's [sounds] (assets/snd).
constexpr const char *RV_SOUND_MUSIC[] = {
    "mus_factory_03.pcm",
    "mus_street_01.pcm",
    "mus_home_01.pcm",
};
static_assert(std::size(RV_SOUND_MUSIC) == static_cast<std::size_t>(rv_sound_area::flat) + 1,
    "one music loop per rv_sound_area");
constexpr const char *RV_SOUND_CLACK = "sfx_board_clack.pcm";

// A voice is one bit of the rv_ca mask; the music and the clack never share one.
constexpr int64_t RV_SOUND_MUSIC_VOICE = 1;
constexpr int64_t RV_SOUND_CLACK_VOICE = 2;

// Envelope and volume of every voice: the shape example-lua gives its tone, at full volume.
constexpr int16_t RV_SOUND_ATTACK = 5;
constexpr int16_t RV_SOUND_DECAY = 40;
constexpr int16_t RV_SOUND_SUSTAIN_RATE = 0;
constexpr int16_t RV_SOUND_RELEASE = 120;
constexpr int16_t RV_SOUND_SUSTAIN_LEVEL = 26000;
constexpr int16_t RV_SOUND_VOLUME = 32767;

// The area of frame `frame`, by the eye x of its camera.
rv_sound_area area_of_frame(const std::vector<rv_route_key> &keys, int64_t frame)
{
    const float eye_x = rv_route_camera_at(keys, frame).eye.x;
    if (eye_x < RV_SOUND_WORKSHOP_MAX_X) {
        return rv_sound_area::workshop;
    }
    if (eye_x > RV_SOUND_FLAT_MIN_X) {
        return rv_sound_area::flat;
    }
    return rv_sound_area::street;
}

// True when some board cell is lit in one of the two frames and not in the other.
bool letters_changed(int64_t from, int64_t to)
{
    for (int i = 0; i < RV_BOARD_CELL_COUNT; ++i) {
        if (rv_board_cell_lit(i, from) != rv_board_cell_lit(i, to)) {
            return true;
        }
    }
    return false;
}

// Plays the sample `name` on `voice` from its start. No voice_stop first: the console answers it with
// RV_ERR_INVAL on a voice not armed yet, and setup then play already restart the voice.
int64_t start_sample(rv_pdko *pdk, const char *name, int64_t voice, rv_loop loop)
{
    const int64_t addr = rv_cd_resource_addr(rv_pdko_cd(pdk), RV_CD_RESOURCE_AUDIO, name);
    if (addr < 0) {
        return addr;
    }
    rv_voice_conf conf = {};
    conf.voice = voice;
    conf.loop_type = loop;
    conf.sample_address = addr;
    conf.ar = RV_SOUND_ATTACK;
    conf.dr = RV_SOUND_DECAY;
    conf.sr = RV_SOUND_SUSTAIN_RATE;
    conf.rr = RV_SOUND_RELEASE;
    conf.sl = RV_SOUND_SUSTAIN_LEVEL;
    conf.volume = RV_SOUND_VOLUME;
    conf.volume_l = RV_SOUND_VOLUME;
    conf.volume_r = RV_SOUND_VOLUME;

    rv_ca *ca = rv_pdko_ca(pdk);
    const int64_t setup_code = rv_ca_voice_setup(ca, &conf);
    if (setup_code < 0) {
        return setup_code;
    }
    return rv_ca_voice_play(ca, voice);
}

} // namespace

int64_t
rv_sound_update(rv_pdko *pdk, const std::vector<rv_route_key> &keys, int64_t frame, bool board_present, rv_sound_state &state)
{
    // The same frame again is no transition: a pause or a repeated render plays nothing.
    if (frame == state.frame) {
        return RV_OK;
    }
    // Both are decided against the previous frame before state takes this one.
    const rv_sound_area area = area_of_frame(keys, frame);
    const bool music_starts = state.frame < 0 || area != state.area;
    const bool in_workshop = area == rv_sound_area::workshop;
    const bool clack = board_present && state.frame >= 0 && in_workshop && letters_changed(state.frame, frame);
    // The seam P-1 -> 0 is no area change inside the workshop, so the music plays on across it.
    if (music_starts) {
        const char *music = RV_SOUND_MUSIC[static_cast<std::size_t>(area)];
        const int64_t music_code = start_sample(pdk, music, RV_SOUND_MUSIC_VOICE, RV_LOOP_FOREVER);
        if (music_code < 0) {
            return music_code;
        }
    }
    // Recorded only after the music start: a failed start is retried on the next frame.
    state.frame = frame;
    state.area = area;
    if (clack) {
        const int64_t clack_code = start_sample(pdk, RV_SOUND_CLACK, RV_SOUND_CLACK_VOICE, RV_LOOP_NONE);
        if (clack_code < 0) {
            return clack_code;
        }
    }
    return RV_OK;
}

} // namespace example_cpp
