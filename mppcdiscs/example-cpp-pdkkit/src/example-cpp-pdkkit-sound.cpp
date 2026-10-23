#include "example-cpp-pdkkit-sound.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_loop.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/sound/rv_pdkkit_sound.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "example-cpp-pdkkit-zones.hpp"

namespace example_cpp_pdkkit
{

namespace
{

// The log tag of the sound messages.
constexpr const char *RV_SOUND_TAG = "sound";

// Holders of T, listed first: a component cannot be added or removed inside ecs_component_each.
template <typename T>
std::vector<rv_pdkkit::ecs_id> holders_of(rv_pdkkit::ecs_world &world)
{
    std::vector<rv_pdkkit::ecs_id> holders;
    rv_pdkkit::ecs_component_each<T>(world, [&holders](rv_pdkkit::ecs_id id, T &) {
        holders.push_back(id);
    });
    return holders;
}

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
// sm_game.cpp:934-936). The loop voice is shared by every zone's loop; the gating is this demo's: a loop runs while the
// listener stands in a zone that names one.
constexpr int64_t RV_SOUND_ASSEMBLY_VOICE = 16;
constexpr int16_t RV_SOUND_ASSEMBLY_VOLUME = 12100;
constexpr int16_t RV_SOUND_LOOP_ENVELOPE = 0;
// A zone's loop names its disc asset without this suffix.
constexpr const char *RV_SOUND_LOOP_SUFFIX = ".pcm";

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

// Stops the loop that plays, then starts `loop` (a zone's loop name, empty for none) on the loop voice. The same loop
// already playing is left alone. Error: the code of the step that fails, with state.error_asset naming the loop.
int64_t switch_loop(rv_pdko *pdk, rv_sound_state &state, const char *loop)
{
    const std::string asset = loop[0] == '\0' ? std::string() : std::string(loop) + RV_SOUND_LOOP_SUFFIX;
    if (asset.size() >= static_cast<std::size_t>(rv_pdkkit::ecs_value_capacity)) {
        return fail(state, loop, RV_ERR_INVAL);
    }
    if (state.loop_armed && asset == state.loop_name) {
        return RV_OK;
    }
    if (state.loop_armed) {
        const int stop_code = rv_pdkkit::sound_voice_stop(rv_pdko_ca(pdk), RV_SOUND_ASSEMBLY_VOICE);
        if (stop_code != RV_OK) {
            return fail(state, state.loop_name, stop_code);
        }
        state.loop_armed = false;
    }
    if (asset.empty()) {
        return RV_OK;
    }
    int64_t addr = 0;
    const int resident_code = rv_pdkkit::sound_asset_resident(rv_pdko_cd(pdk), state.sound, asset.c_str(), addr);
    if (resident_code != RV_OK) {
        return fail(state, asset.c_str(), resident_code);
    }
    rv_voice_conf conf = {};
    conf.voice = RV_SOUND_ASSEMBLY_VOICE;
    conf.loop_type = RV_LOOP_FOREVER;
    conf.sample_address = addr;
    conf.ar = RV_SOUND_LOOP_ENVELOPE;
    conf.dr = RV_SOUND_LOOP_ENVELOPE;
    conf.sr = RV_SOUND_LOOP_ENVELOPE;
    conf.rr = RV_SOUND_LOOP_ENVELOPE;
    conf.sl = RV_SOUND_MAX_LEVEL;
    conf.volume = RV_SOUND_ASSEMBLY_VOLUME;
    conf.volume_l = RV_SOUND_MAX_LEVEL;
    conf.volume_r = RV_SOUND_MAX_LEVEL;
    const int64_t play_code = play_voice(pdk, conf);
    if (play_code < 0) {
        return fail(state, asset.c_str(), play_code);
    }
    set_name(state.loop_name, asset.c_str());
    state.loop_armed = true;
    return RV_OK;
}

// Logs a failure once per asset: a failure repeating in every frame logs once. The logged names are kept in the state;
// past RV_SOUND_LOGGED_CAPACITY names, an asset is logged at each of its failures.
void warn_once(rv_sound_state &state, int64_t code)
{
    for (int i = 0; i < state.logged_count; ++i) {
        if (std::strcmp(state.error_asset, state.logged_assets[i]) == 0) {
            return;
        }
    }
    if (state.logged_count < RV_SOUND_LOGGED_CAPACITY) {
        set_name(state.logged_assets[state.logged_count], state.error_asset);
        ++state.logged_count;
    }
    RV_LOG_WARN(RV_SOUND_TAG, "{} failed: rv_err {}", rv_pdklib::rv_log_escape(state.error_asset), code);
}

// Measures the songs of all zone_music names, each name once, and allocates the music slots. Sets measured, and ready
// when it succeeds.
int64_t measure_songs(rv_pdko *pdk, rv_pdkkit::ecs_world &world, rv_sound_state &state)
{
    std::vector<std::string> names;
    rv_pdkkit::ecs_component_each<ecs_component_zone_music>(world,
        [&names](rv_pdkkit::ecs_id, ecs_component_zone_music &music) {
            const std::string name = music.music;
            if (std::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(name);
            }
        });
    const int64_t code = rv_music_init(pdk, names, state);
    state.measured = true;
    state.ready = code == RV_OK;
    return code;
}

// The listener entered a zone with music: it stands there now, and the zone's music and loop take over where they
// differ from the playing ones. The music itself starts in ecs_system_music's update, which sees the new name.
void enter_zone(rv_pdko *pdk, rv_pdkkit::ecs_id zone, const ecs_component_zone_music &music, ecs_component_listener &listener)
{
    rv_sound_state &state = listener.sound;
    listener.zone = zone;
    if (std::strcmp(music.music, state.song) != 0) {
        RV_LOG_INFO(RV_SOUND_TAG, "music {}", rv_pdklib::rv_log_escape(music.music));
    }
    const int64_t loop_code = switch_loop(pdk, state, music.loop);
    if (loop_code != RV_OK) {
        warn_once(state, loop_code);
    }
}

} // namespace

// Records `asset` as the failure's name for the caller's log and returns `code`.
int64_t fail(rv_sound_state &state, const char *asset, int64_t code)
{
    set_name(state.error_asset, asset);
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

int ecs_system_music(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame)
{
    const std::vector<rv_pdkkit::ecs_id> listeners = holders_of<ecs_component_listener>(world);
    if (listeners.empty()) {
        return RV_OK;
    }
    if (listeners.size() > 1) {
        return RV_ERR_ALREADY;
    }
    const rv_pdkkit::ecs_id listener_id = listeners[0];
    ecs_component_listener *listener = rv_pdkkit::ecs_component_get<ecs_component_listener>(world, listener_id);
    rv_sound_state &state = listener->sound;
    if (!state.measured) {
        const int64_t measure_code = measure_songs(frame.pdk, world, state);
        if (measure_code != RV_OK) {
            warn_once(state, measure_code);
        }
    }
    // Without a measured song set nothing plays, and the failure was logged once above.
    if (!state.ready) {
        return RV_OK;
    }
    const std::vector<rv_pdkkit::ecs_id> crossed = holders_of<ecs_component_zone_crossed>(world);
    for (const rv_pdkkit::ecs_id zone : crossed) {
        const ecs_component_zone_crossed *event = rv_pdkkit::ecs_component_get<ecs_component_zone_crossed>(world, zone);
        if (event->who != listener_id) {
            continue;
        }
        const ecs_component_zone_music *music = rv_pdkkit::ecs_component_get<ecs_component_zone_music>(world, zone);
        if (music == nullptr) {
            continue;
        }
        enter_zone(frame.pdk, zone, *music, *listener);
    }
    // The zone the listener stands in plays its music; with no zone entered yet there is nothing to play.
    const ecs_component_zone_music *current = rv_pdkkit::ecs_component_get<ecs_component_zone_music>(world, listener->zone);
    if (current == nullptr) {
        return RV_OK;
    }
    const int64_t music_code = rv_music_update(frame.pdk, current->music, state);
    if (music_code != RV_OK) {
        warn_once(state, music_code);
    }
    return RV_OK;
}

} // namespace example_cpp_pdkkit
