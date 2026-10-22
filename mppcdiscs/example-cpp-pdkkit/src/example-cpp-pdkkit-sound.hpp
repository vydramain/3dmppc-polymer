#pragma once

#include <cstdint>
#include <vector>

#include "pdk/ca/rv_voice_conf.h"
#include "pdk/rv_pdko.h"
#include "pdkkit/sound/rv_pdkkit_sound.hpp"
#include "pdklib/rv_math/rv_fx.hpp"
#include "example-cpp-pdkkit-route.hpp"

namespace example_cpp_pdkkit
{

// The place the camera is in: the factory (workshop), the street, the flat (apartment).
enum class rv_sound_area : int {
    workshop = 0,
    street = 1,
    flat = 2,
};

// Music slots in sound RAM: a bar plays from one slot while the next bar is written into the other.
constexpr int RV_SOUND_SLOTS = 2;

// The factory loop: it runs while the camera is in the workshop.
constexpr int RV_SOUND_LOOP_COUNT = 1;

// Full level of a voice's sustain and volume: the sound module's maximum, shared by the music and the loops.
constexpr int16_t RV_SOUND_MAX_LEVEL = 32767;

// Music, clack and loop state. Filled once by rv_sound_init; frame = -1 before the first frame.
struct rv_sound_state {
    // The last frame the sound played for, and its area and bar; the bar counts from the area span's first frame.
    int64_t frame = -1;
    rv_sound_area area = rv_sound_area::workshop;
    int64_t bar = -1;
    // Per frame of the period: its area, and the first frame of its area span (cyclic). Empty until built.
    std::vector<rv_sound_area> areas;
    std::vector<int64_t> span_start;
    // Per frame of the period: the cumulative horizontal distance of the eye from frame 0, metres in Q20.12
    // (fx raw, 4096 per metre). Empty until built.
    std::vector<rv_pdklib::fx> distance;
    // Per music slot: sound RAM address, bar asset in it (nullptr when unknown).
    int64_t slot_addr[RV_SOUND_SLOTS] = {};
    const char *slot_name[RV_SOUND_SLOTS] = {};
    // The music chain: the voice reads the slot before music_slot and goes on into music_slot, where its mark is
    // set. music_bar is the bar in music_slot and music_name its asset; music_on is set once a bar is played,
    // music_armed once the repeat and mark on music_slot are set.
    bool music_on = false;
    bool music_armed = false;
    int music_slot = 0;
    int64_t music_bar = 0;
    const char *music_name = nullptr;
    // Per factory loop: whether its voice was started and not stopped since.
    bool loop_armed[RV_SOUND_LOOP_COUNT] = {};
    // One bar read from the disc, slot-sized.
    std::vector<uint8_t> scratch;
    // The clack and loop samples, and the step samples once rv_steps_update runs, made resident by the sound service.
    rv_pdkkit::sound_state sound;
    // The asset of the last failure, for the caller's log.
    const char *error_asset = "";
};

// Measures the music on the disc, allocates the music slots, tabulates the area and span of every frame of the
// period, and tabulates the footstep distance (rv_steps_init) from keys (non-empty, sorted by index). Called once;
// returns RV_OK or the first drive, ca, areas, or step-path error, with state.error_asset naming the asset,
// "sound areas" or "step path". The route fails with RV_ERR_NOENT (no keys) or RV_ERR_INVAL (the Q20.12 math
// leaves its range), for both the areas and the steps.
int64_t rv_sound_init(rv_pdko *pdk, const std::vector<rv_route_key> &keys, rv_sound_state &state);

// Plays what frame `frame` (in [0, RV_ROUTE_PERIOD_FRAMES)) needs after state.frame: the music of its area
// (rv_music_update), the board clack in the workshop when the lit letters differ from state.frame's, and
// only when the scene has the board (board_present). The two factory loops run while the frame is in the
// workshop. The same frame again (a pause, a repeated render) plays nothing. The first frame fills the
// slots and waits for the next bar start when it is mid-bar.
// Needs rv_sound_init first. Returns RV_OK or the drive or ca error, with state.error_asset naming the asset.
int64_t rv_sound_update(rv_pdko *pdk, int64_t frame, bool board_present, rv_sound_state &state);

// Records `asset` as the failure's name for the caller's log and returns `code`.
int64_t fail(rv_sound_state &state, const char *asset, int64_t code);
// Sets up and starts conf.voice (defined in example-cpp-pdkkit-sound.cpp).
int64_t play_voice(rv_pdko *pdk, const rv_voice_conf &conf);

// Music, in example-cpp-pdkkit-music.cpp. rv_music_init allocates the music slots and measures the bars; rv_sound_init
// calls it first. rv_music_update starts the chain at an area change and at a bar start while it is not running,
// moves it on at each mark, and on success records the frame's area and bar; rv_sound_update calls it. Only
// rv_music_update needs the sound tables of rv_sound_init (areas, span_start).
int64_t rv_music_init(rv_pdko *pdk, rv_sound_state &state);
int64_t rv_music_update(rv_pdko *pdk, int64_t frame, rv_sound_state &state);

// Footsteps: one per 0.95 m of horizontal eye path, on the floor of the frame's area, A and B alternating.
// rv_steps_init tabulates the distance (rv_sound_init calls it) and returns RV_OK or the route or fx error, with
// state.error_asset set; rv_steps_update would play the step of a new frame, but rv_sound_update does not call it:
// the call is commented out, so no step sounds. Both need state.areas, which rv_sound_init fills first.
int64_t rv_steps_init(const std::vector<rv_route_key> &keys, rv_sound_state &state);
int64_t rv_steps_update(rv_pdko *pdk, int64_t frame, rv_sound_state &state);

} // namespace example_cpp_pdkkit
