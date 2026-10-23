#include "example-cpp-pdkkit-sound.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_loop.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/rv_err.h"
#include "pdkkit/sound/rv_pdkkit_sound.hpp"
#include "example-cpp-pdkkit-board.hpp"
#include "example-cpp-pdkkit-route.hpp"

namespace example_cpp_pdkkit
{

namespace
{

// Eye x of the camera in Q20.12 raw (4096 per metre), as scenes/main.scene.toml places the objects: the factory is
// at x < 0, the house facade stands at x = 33.8 m = 138444.8 raw. On whole raw values a strict > 138444 is x > 33.8.
using rv_pdklib::fx;
constexpr fx RV_SOUND_WORKSHOP_MAX_X = 0;
constexpr fx RV_SOUND_FLAT_MIN_X = 138444;

// Four takes of the lamp click, each shaped by a resonator measured from a recorded bad lamp: three rings near 2 kHz
// and a short tick. play_clack picks one from clack_rng, never from the frame.
constexpr const char *RV_SOUND_CLACKS[] = {
    "sfx_lamp_click_1.pcm",
    "sfx_lamp_click_2.pcm",
    "sfx_lamp_click_3.pcm",
    "sfx_lamp_click_4.pcm",
};
static_assert(std::size(RV_SOUND_CLACKS) == RV_SOUND_CLACK_COUNT, "one name per clack take");

// Shifts of Marsaglia's xorshift32 (13, 17, 5): a full-period generator of nonzero 32-bit states,
// cheap enough for every clack.
constexpr int RV_XORSHIFT_LEFT_1 = 13;
constexpr int RV_XORSHIFT_RIGHT = 17;
constexpr int RV_XORSHIFT_LEFT_2 = 5;

// The clack's voice.
constexpr int64_t RV_SOUND_CLACK_VOICE = 4;

// Clack envelope and volume: the shape example-lua-bare gives its tone, at full volume.
constexpr int16_t RV_SOUND_CLACK_ATTACK = 5;
constexpr int16_t RV_SOUND_CLACK_DECAY = 40;
constexpr int16_t RV_SOUND_CLACK_SUSTAIN_RATE = 0;
constexpr int16_t RV_SOUND_CLACK_RELEASE = 120;
constexpr int16_t RV_SOUND_CLACK_SUSTAIN_LEVEL = 26000;

// Volume, loop type and envelope as Solid Maid sets them for its factory loop (sm_sound.cpp:127-129, :410-452;
// sm_game.cpp:934-936). The gating is this demo's: the loop runs while the camera is in the workshop, with no
// assembly-working state.
constexpr int64_t RV_SOUND_ASSEMBLY_VOICE = 16;
constexpr int16_t RV_SOUND_ASSEMBLY_VOLUME = 12100;
constexpr int16_t RV_SOUND_LOOP_ENVELOPE = 0;
constexpr const char *RV_SOUND_ASSEMBLY_LOOP = "sfx_loop_assembly.pcm";

struct rv_sound_loop {
    const char *name;
    int64_t voice;
    int16_t volume;
};
constexpr rv_sound_loop RV_SOUND_LOOPS[] = {
    { RV_SOUND_ASSEMBLY_LOOP, RV_SOUND_ASSEMBLY_VOICE, RV_SOUND_ASSEMBLY_VOLUME },
};
static_assert(std::size(RV_SOUND_LOOPS) == RV_SOUND_LOOP_COUNT, "one entry per loop");

// The song each area plays: its bars are mus_<song>_NN.pcm, counted on the disc by rv_music_init. Order follows rv_sound_area.
constexpr const char *RV_SOUND_SONGS[] = {
    "factory",
    "street",
    "home",
};
static_assert(std::size(RV_SOUND_SONGS) == static_cast<std::size_t>(rv_sound_area::flat) + 1, "one song per area");

// The area of frame `frame`, by the eye x of its camera. Error: the route fails (rv_route_eye_at); out is untouched.
int area_of_frame(const std::vector<rv_route_key> &keys, int64_t frame, rv_sound_area &out)
{
    rv_pdklib::fx_vec3 eye = {};
    const int eye_code = rv_route_eye_at(keys, frame, eye);
    if (eye_code != RV_OK) {
        return eye_code;
    }
    if (eye.x < RV_SOUND_WORKSHOP_MAX_X) {
        out = rv_sound_area::workshop;
        return RV_OK;
    }
    out = eye.x > RV_SOUND_FLAT_MIN_X ? rv_sound_area::flat : rv_sound_area::street;
    return RV_OK;
}

// True when frame f starts an area span: its area differs from the frame before it, cyclically.
bool starts_span(const std::vector<rv_sound_area> &areas, int64_t f)
{
    const int64_t period = static_cast<int64_t>(areas.size());
    return areas[static_cast<std::size_t>(f)] != areas[static_cast<std::size_t>((f + period - 1) % period)];
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

// Advances the clack generator once (xorshift32) and returns the index of the take it picks.
int clack_take(rv_sound_state &state)
{
    uint32_t x = state.clack_rng;
    x ^= x << RV_XORSHIFT_LEFT_1;
    x ^= x >> RV_XORSHIFT_RIGHT;
    x ^= x << RV_XORSHIFT_LEFT_2;
    state.clack_rng = x;
    return static_cast<int>(x % static_cast<uint32_t>(RV_SOUND_CLACK_COUNT));
}

int64_t play_clack(rv_pdko *pdk, rv_sound_state &state)
{
    const char *clack = RV_SOUND_CLACKS[clack_take(state)];
    int64_t addr = 0;
    const int resident_code = rv_pdkkit::sound_asset_resident(rv_pdko_cd(pdk), state.sound, clack, addr);
    if (resident_code != RV_OK) {
        return fail(state, clack, resident_code);
    }
    // The click is off: its play_voice call below is commented out; conf is kept for when it comes back.
    [[maybe_unused]] rv_voice_conf conf = {};
    conf.voice = RV_SOUND_CLACK_VOICE;
    conf.loop_type = RV_LOOP_NONE;
    conf.sample_address = addr;
    conf.ar = RV_SOUND_CLACK_ATTACK;
    conf.dr = RV_SOUND_CLACK_DECAY;
    conf.sr = RV_SOUND_CLACK_SUSTAIN_RATE;
    conf.rr = RV_SOUND_CLACK_RELEASE;
    conf.sl = RV_SOUND_CLACK_SUSTAIN_LEVEL;
    conf.volume = RV_SOUND_MAX_LEVEL;
    conf.volume_l = RV_SOUND_MAX_LEVEL;
    conf.volume_r = RV_SOUND_MAX_LEVEL;
    // const int64_t play_code = play_voice(pdk, conf);
    // if (play_code < 0) {
    //     return fail(state, clack, play_code);
    // }
    return RV_OK;
}

// Starts loop `index` when `on`, stops it otherwise. A stop is sent only to a loop this state started, since the
// console refuses a stop on a voice not armed. A loop already in the wanted state does nothing.
int64_t set_loop(rv_pdko *pdk, rv_sound_state &state, int index, bool on)
{
    const rv_sound_loop &loop = RV_SOUND_LOOPS[index];
    if (on == state.loop_armed[index]) {
        return RV_OK;
    }
    if (!on) {
        const int stop_code = rv_pdkkit::sound_voice_stop(rv_pdko_ca(pdk), loop.voice);
        if (stop_code != RV_OK) {
            return fail(state, loop.name, stop_code);
        }
        state.loop_armed[index] = false;
        return RV_OK;
    }
    int64_t addr = 0;
    const int resident_code = rv_pdkkit::sound_asset_resident(rv_pdko_cd(pdk), state.sound, loop.name, addr);
    if (resident_code != RV_OK) {
        return fail(state, loop.name, resident_code);
    }
    rv_voice_conf conf = {};
    conf.voice = loop.voice;
    conf.loop_type = RV_LOOP_FOREVER;
    conf.sample_address = addr;
    conf.ar = RV_SOUND_LOOP_ENVELOPE;
    conf.dr = RV_SOUND_LOOP_ENVELOPE;
    conf.sr = RV_SOUND_LOOP_ENVELOPE;
    conf.rr = RV_SOUND_LOOP_ENVELOPE;
    conf.sl = RV_SOUND_MAX_LEVEL;
    conf.volume = loop.volume;
    conf.volume_l = RV_SOUND_MAX_LEVEL;
    conf.volume_r = RV_SOUND_MAX_LEVEL;
    const int64_t play_code = play_voice(pdk, conf);
    if (play_code < 0) {
        return fail(state, loop.name, play_code);
    }
    state.loop_armed[index] = true;
    return RV_OK;
}

// Keeps the loops playing while `in_workshop`, and silent otherwise. Every loop is tried; the first failure is returned.
int64_t set_loops(rv_pdko *pdk, rv_sound_state &state, bool in_workshop)
{
    int64_t code = RV_OK;
    std::string asset;
    for (int i = 0; i < RV_SOUND_LOOP_COUNT; ++i) {
        const int64_t loop_code = set_loop(pdk, state, i, in_workshop);
        if (code == RV_OK && loop_code < 0) {
            code = loop_code;
            asset = state.error_asset;
        }
    }
    state.error_asset = asset;
    return code;
}

} // namespace

// Records `asset` as the failure's name for the caller's log and returns `code`.
int64_t fail(rv_sound_state &state, const char *asset, int64_t code)
{
    state.error_asset = asset;
    return code;
}

// Sets up and starts conf.voice. No voice_stop first: the console answers it with RV_ERR_INVAL on a
// voice not armed yet, and setup then play already restart the voice.
int64_t play_voice(rv_pdko *pdk, const rv_voice_conf &conf)
{
    rv_ca *ca = rv_pdko_ca(pdk);
    const int setup_code = rv_pdkkit::sound_voice_setup(ca, conf);
    if (setup_code != RV_OK) {
        return setup_code;
    }
    return rv_pdkkit::sound_voice_play(ca, conf.voice);
}

int64_t rv_sound_init(rv_pdko *pdk, const std::vector<rv_route_key> &keys, rv_sound_areas &areas, rv_sound_state &state)
{
    // The songs are measured here, one per area in area order.
    const std::vector<std::string> songs(std::begin(RV_SOUND_SONGS), std::end(RV_SOUND_SONGS));
    const int64_t music_code = rv_music_init(pdk, songs, state);
    if (music_code < 0) {
        return music_code;
    }

    // Each frame's area, and the first frame of its span. Spans wrap: the workshop may cross the seam
    // from the last frame of the period to the first, so the walk starts at an area change and goes round once.
    const int64_t period = RV_ROUTE_PERIOD_FRAMES;
    areas.frame_area.resize(static_cast<std::size_t>(period));
    areas.span_start.assign(static_cast<std::size_t>(period), 0);
    for (int64_t f = 0; f < period; ++f) {
        const int area_code = area_of_frame(keys, f, areas.frame_area[static_cast<std::size_t>(f)]);
        if (area_code != RV_OK) {
            return fail(state, "sound areas", area_code);
        }
    }
    int64_t change = -1;
    for (int64_t f = 0; f < period && change < 0; ++f) {
        if (starts_span(areas.frame_area, f)) {
            change = f;
        }
    }
    // With no change at all the one area covers the period and every span starts at frame 0.
    if (change >= 0) {
        int64_t start = change;
        for (int64_t i = 0; i < period; ++i) {
            const int64_t f = (change + i) % period;
            if (starts_span(areas.frame_area, f)) {
                start = f;
            }
            areas.span_start[static_cast<std::size_t>(f)] = start;
        }
    }
    return RV_OK;
}

int64_t rv_sound_update(rv_pdko *pdk, int64_t frame, bool board_present, const rv_sound_areas &areas, rv_sound_state &state)
{
    const int64_t period = RV_ROUTE_PERIOD_FRAMES;
    if (areas.frame_area.size() != static_cast<std::size_t>(period)) {
        return fail(state, "sound tables", RV_ERR_INVAL);
    }
    // The same frame again is no transition: a pause or a repeated render plays nothing.
    if (frame == state.frame) {
        return RV_OK;
    }
    const rv_sound_area area = areas.frame_area[static_cast<std::size_t>(frame)];
    const int64_t span_offset = (frame - areas.span_start[static_cast<std::size_t>(frame)] + period) % period;
    // Decided against the previous frame before the music takes this one.
    const bool first = state.frame < 0;
    const bool clack = board_present && !first && area == rv_sound_area::workshop && letters_changed(state.frame, frame);
    // The loops always run. The clack runs only after the music step succeeded: otherwise the same clack would
    // repeat every frame. The first failure is returned with its asset.
    int64_t code = RV_OK;
    std::string asset;
    const char *song = RV_SOUND_SONGS[static_cast<std::size_t>(area)];
    const int64_t music_code = rv_music_update(pdk, song, span_offset, state);
    if (music_code < 0) {
        code = music_code;
        asset = state.error_asset;
    }
    // Recorded only after the music start: a failed start is retried on the next rv_sound_update call
    // (the next frame, or the same frame again while paused).
    const bool music_ok = music_code == RV_OK;
    if (music_ok) {
        state.frame = frame;
    }
    // Checked every new frame, not only on a change: a failed start is then retried, and a loop already right does nothing.
    const int64_t loops_code = set_loops(pdk, state, area == rv_sound_area::workshop);
    if (code == RV_OK && loops_code < 0) {
        code = loops_code;
        asset = state.error_asset;
    }
    if (music_ok && clack) {
        const int64_t clack_code = play_clack(pdk, state);
        if (code == RV_OK && clack_code < 0) {
            code = clack_code;
            asset = state.error_asset;
        }
    }
    state.error_asset = asset;
    return code;
}

} // namespace example_cpp_pdkkit
