#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "pdk/ca/rv_voice_conf.h"
#include "pdk/rv_pdko.h"
#include "pdkkit/component/rv_component_capacity.hpp"
#include "pdkkit/component/rv_component_macro.hpp"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_frame.hpp"
#include "pdkkit/sound/rv_pdkkit_sound.hpp"

namespace example_cpp_pdkkit
{

// The board clack takes: play_clack picks one per clack from a fixed-seed generator.
constexpr int RV_SOUND_CLACK_COUNT = 4;

// Full level of the sound module: the stereo volume of the clacks and loops and the sustain of the loops.
constexpr int16_t RV_SOUND_MAX_LEVEL = 32767;

// Seed of the clack generator (xorshift32). Zero is its fixed point, so the seed must be nonzero.
constexpr uint32_t RV_SOUND_CLACK_SEED = 0x2545F491u;

// Copies `src` into the NUL-terminated field `dst`, cut to fit. Callers check a name fits where a cut would lose it.
template <std::size_t N>
void set_name(char (&dst)[N], const char *src)
{
    std::snprintf(dst, N, "%s", src);
}

// Failures logged per asset the state can remember, in a fixed array. The names this disc can log: 3 music streams
// (mus_factory.pcm, mus_home.pcm, mus_street.pcm), mus_.pcm when the stream status read fails before the first play
// (empty song, null drive), 4 clacks,
// the loop (sfx_loop_assembly.pcm), and 2 refusal names ("stream name", "stream volume"): 11.
// Recount when the streams, clacks or loops change.
constexpr int RV_SOUND_LOGGED_CAPACITY = 11;

// Music, clack and loop state: the working state of the listener.
struct rv_sound_state {
    // The zone_music name the stream plays; its disc asset is mus_<song>.pcm. Empty before the first play.
    char song[rv_pdkkit::ecs_value_capacity]{};
    // Set once the stream volume is set; a failed set is retried on the next frame.
    bool volume_set = false;
    // The loop voice: whether a loop was started on it and not stopped since, and the loop's asset.
    bool loop_armed = false;
    char loop_name[rv_pdkkit::ecs_value_capacity]{};
    // The clack generator's xorshift32 state; play_clack advances it once per clack.
    uint32_t clack_rng = RV_SOUND_CLACK_SEED;
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
// Plays one board clack from clack_rng (defined in the .cpp). ecs_system_clack calls it.
int64_t play_clack(rv_pdko *pdk, rv_sound_state &state);

// Music of a zone: the stream played there (mus_<music>.pcm) and the looped asset played there;
// an empty loop means none.
struct ecs_component_zone_music {
    char music[rv_pdkkit::ecs_value_capacity]{};
    char loop[rv_pdkkit::ecs_value_capacity]{};
};

// Marks the zone where the sign's clack is heard.
struct ecs_component_zone_clack {};

// The listener, set on the eye. Its working state is not a scene field: the music system keeps the sound state and the
// zone the listener is in, which it writes when the zone changes; the clack system reads that zone.
struct ecs_component_listener {
    rv_sound_state sound;
    rv_pdkkit::ecs_id zone{};
};

// Frame system: plays the listener's music and loop. On each zone_crossed of the listener in a zone with zone_music, the
// listener stands in that zone and its loop takes over where it differs. The zone's music streams where it differs from
// the playing one; the stream volume is set on the first frame that succeeds. Every frame reads the stream state: an
// ERROR is logged once per asset while the logged names fit RV_SOUND_LOGGED_CAPACITY. Runs after the zones system.
// A sound failure is not returned; it is logged once per asset while the names fit RV_SOUND_LOGGED_CAPACITY.
// Error: RV_ERR_ALREADY with two listeners.
int ecs_system_music(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

// Frame system: plays one clack in the frame when a lamp switched (flicker_switched) and the listener stands in a zone
// with zone_clack. Runs after music and flicker, so it sees this frame's zone and switches. A clack failure is not
// returned; it is logged once per asset while the names fit RV_SOUND_LOGGED_CAPACITY. Error: RV_ERR_ALREADY with two listeners.
int ecs_system_clack(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame);

} // namespace example_cpp_pdkkit

PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_zone_music,
    "zone_music",
    PDKKIT_FIELD(music, string, ""),
    PDKKIT_FIELD(loop, string, ""));
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_zone_clack, "zone_clack");
PDKKIT_COMPONENT(example_cpp_pdkkit::ecs_component_listener, "listener");
