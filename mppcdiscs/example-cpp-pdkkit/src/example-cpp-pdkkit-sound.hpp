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
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdkkit/sound/rv_pdkkit_sound.hpp"

namespace example_cpp_pdkkit
{

// Music slots in sound RAM: a bar plays from one slot while the next bar is written into the other.
constexpr int RV_SOUND_SLOTS = 2;

// The board clack takes: play_clack picks one per clack from a fixed-seed generator.
constexpr int RV_SOUND_CLACK_COUNT = 4;

// Full level of a voice's sustain and volume: the sound module's maximum, shared by the music and the loops.
constexpr int16_t RV_SOUND_MAX_LEVEL = 32767;

// Seed of the clack generator (xorshift32). Zero is its fixed point, so the seed must be nonzero.
constexpr uint32_t RV_SOUND_CLACK_SEED = 0x2545F491u;

// Songs a state holds: one per zone_music name. rv_music_init refuses more with RV_ERR_NOMEM.
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

// Failures logged per asset the state can remember, in a fixed array. The count is what can fail on the disc: 18 bars
// (factory 3, street 5, home 10), 1 loop, and 4 refusal names ("music songs", "music slot", "music song", "music
// mark"): 23. Recount when the bars grow.
constexpr int RV_SOUND_LOGGED_CAPACITY = 23;

// Music, clack and loop state: the working state of the listener. ecs_system_music measures the songs once.
struct rv_sound_state {
    // The song rv_music_update last played.
    char song[rv_pdkkit::ecs_value_capacity]{};
    // The songs rv_music_init measured, in the order it got them: the first song_count entries.
    rv_music_song songs[RV_SOUND_SONG_CAPACITY] = {};
    int64_t song_count = 0;
    // Set once rv_music_init has run; ready when it succeeded. Nothing plays unless ready.
    bool measured = false;
    bool ready = false;
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
    // The loop voice: whether a loop was started on it and not stopped since, and the loop's asset.
    bool loop_armed = false;
    char loop_name[rv_pdkkit::ecs_value_capacity]{};
    // The clack generator's xorshift32 state; play_clack advances it once per clack.
    uint32_t clack_rng = RV_SOUND_CLACK_SEED;
    // The largest bar on the disc, which is the size of each music slot: a bar read from the disc must fit it.
    int64_t slot_bytes = 0;
    // The clack and loop samples, made resident by the sound service.
    rv_pdkkit::sound_state sound;
    // The asset of the last failure, for the caller's log.
    char error_asset[rv_pdkkit::ecs_value_capacity]{};
    // The assets whose failure was logged: the first logged_count entries. The array keeps the state fixed-size and
    // trivially copyable; an asset beyond RV_SOUND_LOGGED_CAPACITY names is logged at each of its failures.
    char logged_assets[RV_SOUND_LOGGED_CAPACITY][rv_pdkkit::ecs_value_capacity]{};
    int logged_count = 0;
};

static_assert(std::is_standard_layout_v<rv_sound_state> && std::is_trivially_copyable_v<rv_sound_state>,
    "rv_sound_state is a component's working state: standard layout and trivially copyable");

// Records `asset` as the failure's name for the caller's log and returns `code`; a name longer than the field is cut.
int64_t fail(rv_sound_state &state, const char *asset, int64_t code);
// Sets up and starts conf.voice (defined in example-cpp-pdkkit-sound.cpp).
int64_t play_voice(rv_pdko *pdk, const rv_voice_conf &conf);
// Plays one board clack from clack_rng. Called by the clack system, which is not written yet (defined in the .cpp).
int64_t play_clack(rv_pdko *pdk, rv_sound_state &state);

// Music, in example-cpp-pdkkit-music.cpp. rv_music_init measures the bars of each named song and allocates the music
// slots; ecs_system_music calls it once. rv_music_update starts the song's first bar at a song change, moves the chain on
// at each mark, and on success records the song; ecs_system_music calls it every frame.
// Precondition: name is a song rv_music_init measured. Otherwise RV_ERR_INVAL, with state.error_asset "music song".
// Returns RV_OK or the first error: a disc code from a bar's open or size; RV_ERR_NOENT for a song with no bars;
// RV_ERR_INVAL for an empty bar or one larger than the slot; RV_ERR_IO when the bar's read gave no bytes or failed (its
// own code is not passed on); a ca or voice code from the mark read, the bar write, the repeat and mark, or the play.
// state.error_asset names the bar, or "music mark" for the mark read.
int64_t rv_music_init(rv_pdko *pdk, const std::vector<std::string> &names, rv_sound_state &state);
int64_t rv_music_update(rv_pdko *pdk, const char *name, rv_sound_state &state);

// Music of a zone: the place its bars come from (mus_<music>_NN.pcm) and the looped asset played there;
// an empty loop means none.
struct ecs_component_zone_music {
    char music[rv_pdkkit::ecs_value_capacity]{};
    char loop[rv_pdkkit::ecs_value_capacity]{};
};

// Marks the zone where the sign's clack is heard.
struct ecs_component_zone_clack {
};

// The listener, set on the eye. Its working state is not a scene field: the music system keeps the sound state and the
// zone the listener is in, which it writes when the zone changes; the clack system reads that zone.
struct ecs_component_listener {
    rv_sound_state sound;
    rv_pdkkit::ecs_id zone{};
};

// Frame system: plays the listener's music and loop. Measures the zone_music songs at the listener's first frame; then, on
// each zone_crossed of the listener in a zone with zone_music, the listener stands in that zone and its music and loop
// take over where they differ. Runs after the zones system. A sound failure is logged once per asset and not returned.
// Error: RV_ERR_ALREADY with two listeners.
int ecs_system_music(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

} // namespace example_cpp_pdkkit

PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_zone_music,
    "zone_music",
    PDKKIT_FIELD(music, string, ""),
    PDKKIT_FIELD(loop, string, ""));
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_zone_clack, "zone_clack");
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_listener, "listener");
