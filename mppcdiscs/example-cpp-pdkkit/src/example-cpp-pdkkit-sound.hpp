#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include "pdk/ca/rv_voice_conf.h"
#include "pdk/rv_pdko.h"
#include "pdkkit/component/rv_component_capacity.hpp"
#include "pdkkit/component/rv_component_macro.hpp"
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

// Songs a state holds: the demo's three, one per area. rv_music_init refuses more with RV_ERR_NOMEM.
constexpr std::size_t RV_SOUND_SONG_CAPACITY = 3;

// Copies `src` into the NUL-terminated field `dst`, cut to fit. Callers check a name fits where a cut would lose it.
template <std::size_t N>
void set_name(char (&dst)[N], const char *src)
{
    std::snprintf(dst, N, "%s", src);
}

// A song the music plays: bar k (from 0) is the disc asset mus_<name>_NN.pcm, NN = k + 1 at least two digits; bars counts them.
// For a song of up to 99 bars the name is at most 52 characters: its bar asset mus_<name>_NN.pcm adds 11 (four, one,
// two digits, four) to fit the field. rv_music_init refuses a longer name, and a bar name that does not fit.
struct rv_music_song {
    char name[rv_pdkkit::ecs_value_capacity]{};
    int64_t bars = 0;
};

// The place of each frame of the period: its area, and the first frame of its area span (cyclic). Filled once by
// rv_sound_init and read by rv_sound_update; owned by the disc.
struct rv_sound_areas {
    std::vector<rv_sound_area> frame_area;
    std::vector<int64_t> span_start;
};

// Music, clack and loop state. Filled once by rv_sound_init; frame = -1 before the first frame.
struct rv_sound_state {
    // The last frame the sound played for, and its song and bar; the bar counts from the area span's first frame.
    int64_t frame = -1;
    char song[rv_pdkkit::ecs_value_capacity]{};
    int64_t bar = -1;
    // The songs rv_music_init measured, in the order it got them: the first song_count entries.
    rv_music_song songs[RV_SOUND_SONG_CAPACITY] = {};
    int64_t song_count = 0;
    // Per music slot: sound RAM address, bar asset in it (empty when unknown).
    int64_t slot_addr[RV_SOUND_SLOTS] = {};
    char slot_name[RV_SOUND_SLOTS][rv_pdkkit::ecs_value_capacity] = {};
    // The music chain: the voice reads the slot before music_slot and goes on into music_slot, where its mark is
    // set. music_bar is the bar in music_slot and music_name its asset; music_on is set once a bar is played,
    // music_armed once the repeat and mark on music_slot are set.
    bool music_on = false;
    bool music_armed = false;
    int music_slot = 0;
    int64_t music_bar = 0;
    char music_name[rv_pdkkit::ecs_value_capacity]{};
    // Per factory loop: whether its voice was started and not stopped since.
    bool loop_armed[RV_SOUND_LOOP_COUNT] = {};
    // The clack generator's xorshift32 state; play_clack advances it once per clack.
    uint32_t clack_rng = RV_SOUND_CLACK_SEED;
    // The largest bar on the disc, which is the size of each music slot: a bar read from the disc must fit it.
    int64_t slot_bytes = 0;
    // The clack and loop samples, made resident by the sound service.
    rv_pdkkit::sound_state sound;
    // The asset of the last failure, for the caller's log.
    char error_asset[rv_pdkkit::ecs_value_capacity]{};
};

static_assert(std::is_standard_layout_v<rv_sound_state> && std::is_trivially_copyable_v<rv_sound_state>,
    "rv_sound_state is a component's working state: standard layout and trivially copyable");

// Measures the music of each area's song on the disc, allocates the music slots, and tabulates the area and span of
// every frame of the period into areas, from keys (non-empty, sorted by index). Called once; returns RV_OK or the first error.
// A music error is rv_music_init's: a disc code from a bar's open or size, RV_ERR_INVAL for an empty bar, or RV_ERR_NOENT
// for a song with no bars, with state.error_asset set to the bar; a ca code from the slot allocation, with "music
// slot". An area error is the route's: RV_ERR_NOENT (no keys) or RV_ERR_INVAL (the Q20.12 math leaves its range), with
// state.error_asset set to "sound areas".
int64_t rv_sound_init(rv_pdko *pdk, const std::vector<rv_route_key> &keys, rv_sound_areas &areas, rv_sound_state &state);

// Plays what frame `frame` (in [0, RV_ROUTE_PERIOD_FRAMES)) needs after state.frame, with the areas rv_sound_init filled:
// the music of its area (rv_music_update), the board clack in the workshop when the lit letters differ from state.frame's,
// and only when the scene has the board (board_present). The two factory loops run while the frame is in the workshop.
// The same frame again (a pause, a repeated render) plays nothing. The first frame fills the slots and waits for the next
// bar start when it is mid-bar. Needs rv_sound_init first. Returns RV_OK or the drive or ca error, with state.error_asset
// naming the asset.
int64_t rv_sound_update(rv_pdko *pdk, int64_t frame, bool board_present, const rv_sound_areas &areas, rv_sound_state &state);

// Records `asset` as the failure's name for the caller's log and returns `code`; a name longer than the field is cut.
int64_t fail(rv_sound_state &state, const char *asset, int64_t code);
// Sets up and starts conf.voice (defined in example-cpp-pdkkit-sound.cpp).
int64_t play_voice(rv_pdko *pdk, const rv_voice_conf &conf);

// Music, in example-cpp-pdkkit-music.cpp. rv_music_init measures the bars of each named song and allocates the music
// slots; rv_sound_init calls it first. rv_music_update starts the chain at a song change and at a bar start while it is
// not running, moves it on at each mark, and on success records the song and bar; rv_sound_update calls it.
// Precondition: name is a song rv_music_init measured, and span_offset is in [0, RV_ROUTE_PERIOD_FRAMES), the frames
// since the area span's first frame; its bar is span_offset / RV_SOUND_BAR_FRAMES. Otherwise RV_ERR_INVAL, with
// state.error_asset "music song" or "music offset". Reads none of the frame tables (rv_sound_areas). Returns RV_OK
// or the first error: a disc code from a bar's open or size; RV_ERR_NOENT for a song with no bars; RV_ERR_INVAL for an
// empty bar or one larger than the slot; RV_ERR_IO when the bar's read gave no bytes or failed (its own code is not
// passed on); a ca or voice code from the mark read, the bar write, the repeat and mark, or the play. state.error_asset
// names the bar, or "music mark" for the mark read.
int64_t rv_music_init(rv_pdko *pdk, const std::vector<std::string> &names, rv_sound_state &state);
int64_t rv_music_update(rv_pdko *pdk, const char *name, int64_t span_offset, rv_sound_state &state);

// Music of a zone: the place its bars come from (mus_<music>_NN.pcm) and the looped asset played there;
// an empty loop means none.
struct ecs_component_zone_music {
    char music[rv_pdkkit::ecs_value_capacity]{};
    char loop[rv_pdkkit::ecs_value_capacity]{};
};

// Marks the zone where the sign's clack is heard.
struct ecs_component_zone_clack {
};

} // namespace example_cpp_pdkkit

PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_zone_music,
    "zone_music",
    PDKKIT_FIELD(music, string, ""),
    PDKKIT_FIELD(loop, string, ""));
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_zone_clack, "zone_clack");
