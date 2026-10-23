#pragma once

#include <cstdint>
#include <vector>

#include "pdk/ca/rv_voice_conf.h"
#include "pdk/rv_pdko.h"
#include "pdkkit/sound/rv_pdkkit_sound.hpp"
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

// The board clack takes: play_clack picks one per clack from a fixed-seed generator.
constexpr int RV_SOUND_CLACK_COUNT = 4;

// Full level of a voice's sustain and volume: the sound module's maximum, shared by the music and the loops.
constexpr int16_t RV_SOUND_MAX_LEVEL = 32767;

// Seed of the clack generator (xorshift32). Zero is its fixed point, so the seed must be nonzero.
constexpr uint32_t RV_SOUND_CLACK_SEED = 0x2545F491u;

// Music, clack and loop state. Filled once by rv_sound_init; frame = -1 before the first frame.
struct rv_sound_state {
    // The last frame the sound played for, and its area and bar; the bar counts from the area span's first frame.
    int64_t frame = -1;
    rv_sound_area area = rv_sound_area::workshop;
    int64_t bar = -1;
    // Per frame of the period: its area, and the first frame of its area span (cyclic). Empty until built.
    std::vector<rv_sound_area> areas;
    std::vector<int64_t> span_start;
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
    // The clack generator's xorshift32 state; play_clack advances it once per clack.
    uint32_t clack_rng = RV_SOUND_CLACK_SEED;
    // One bar read from the disc, slot-sized.
    std::vector<uint8_t> scratch;
    // The clack and loop samples, made resident by the sound service.
    rv_pdkkit::sound_state sound;
    // The asset of the last failure, for the caller's log.
    const char *error_asset = "";
};

// Measures the music on the disc, allocates the music slots, and tabulates the area and span of every frame of the
// period from keys (non-empty, sorted by index). Called once; returns RV_OK or the first error. A music error is
// rv_music_init's: a disc code from the bar's open or size, or RV_ERR_INVAL for an empty bar, with state.error_asset
// set to the bar; a ca code from the slot allocation, with "music slot". An area error is the route's: RV_ERR_NOENT
// (no keys) or RV_ERR_INVAL (the Q20.12 math leaves its range), with state.error_asset set to "sound areas".
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
// moves it on at each mark, and on success records the area and bar; rv_sound_update calls it. Precondition: area is a
// rv_sound_area and span_offset is in [0, RV_ROUTE_PERIOD_FRAMES), the frames since the area span's first frame; its
// bar is span_offset / RV_SOUND_BAR_FRAMES. Otherwise RV_ERR_INVAL, with state.error_asset "music area" or "music
// offset". Reads none of the sound tables (areas, span_start). Returns RV_OK or the first error: a disc code from the
// bar's open or size; RV_ERR_INVAL for an empty bar or one larger than the slot; RV_ERR_IO when the bar's read gave no
// bytes or failed (its own code is not passed on); a ca or voice code from the mark read, the bar write, the repeat
// and mark, or the play. state.error_asset names the bar, or "music mark" for the mark read.
int64_t rv_music_init(rv_pdko *pdk, rv_sound_state &state);
int64_t rv_music_update(rv_pdko *pdk, rv_sound_area area, int64_t span_offset, rv_sound_state &state);

} // namespace example_cpp_pdkkit
