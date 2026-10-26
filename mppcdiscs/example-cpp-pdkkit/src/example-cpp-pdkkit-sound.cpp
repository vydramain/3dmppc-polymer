#include "example-cpp-pdkkit-sound.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_loop.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"
#include "pdkkit/ecs/rv_ecs_component.hpp"
#include "pdkkit/sound/rv_pdkkit_sound.hpp"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "example-cpp-pdkkit-board.hpp"
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

// A music is the disc entry mus_<music>.pcm, played as one stream.
constexpr const char *RV_MUSIC_PREFIX = "mus_";
constexpr const char *RV_MUSIC_SUFFIX = ".pcm";
// rv_cd_stream_conf loop: 1 repeats the loop, which is the whole entry (loop_start 0, loop_end 0 from the zero init).
constexpr int64_t RV_MUSIC_LOOP = 1;
// Stream input volume of the music, set once: 10000 of the 32767 full level.
constexpr int16_t RV_MUSIC_VOLUME = 10000;

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

// The disc entry of a music's stream.
std::string stream_asset(const char *music)
{
    return std::string(RV_MUSIC_PREFIX) + music + RV_MUSIC_SUFFIX;
}

// Starts the stream of `music` from its first frame, looped over the whole entry. A refusal changes nothing: the playing
// stream and state.song stay, and the next frame tries again.
int64_t play_stream(rv_pdko *pdk, rv_sound_state &state, const char *music)
{
    const std::string asset = stream_asset(music);
    if (asset.size() >= static_cast<std::size_t>(rv_pdkkit::ecs_value_capacity)) {
        return fail(state, "stream name", RV_ERR_INVAL);
    }
    rv_cd_stream_conf conf = {};
    conf.loop = RV_MUSIC_LOOP;
    const int64_t play_code = rv_cd_stream_play(rv_pdko_cd(pdk), asset.c_str(), &conf);
    if (play_code != RV_OK) {
        return fail(state, asset.c_str(), play_code);
    }
    set_name(state.song, music);
    return RV_OK;
}

// Starts the zone's music when it differs from the playing one.
int64_t play_music(rv_pdko *pdk, rv_sound_state &state, const char *music)
{
    if (std::strcmp(music, state.song) == 0) {
        return RV_OK;
    }
    return play_stream(pdk, state, music);
}

// Reads the stream state every frame. An ERROR, or a failed read of it, is returned under the playing stream's asset.
int64_t check_stream(rv_pdko *pdk, rv_sound_state &state)
{
    rv_cd_stream_info info = {};
    const int64_t status_code = rv_cd_stream_status(rv_pdko_cd(pdk), &info);
    if (status_code == RV_OK && info.state != RV_CD_STREAM_ERROR) {
        return RV_OK;
    }
    const std::string asset = stream_asset(state.song);
    const int64_t code = status_code != RV_OK ? status_code : info.error;
    return fail(state, asset.c_str(), code);
}

// Sets the stream volume once; a failed set is retried on the next frame.
int64_t set_volume(rv_pdko *pdk, rv_sound_state &state)
{
    if (state.volume_set) {
        return RV_OK;
    }
    const int64_t volume_code = rv_ca_stream_volume(rv_pdko_ca(pdk), RV_MUSIC_VOLUME, RV_MUSIC_VOLUME);
    if (volume_code != RV_OK) {
        return fail(state, "stream volume", volume_code);
    }
    state.volume_set = true;
    return RV_OK;
}

// The listener entered a zone with music: it stands there now, and the zone's loop takes over where it differs. The
// music itself starts in ecs_system_music, which sees the new zone.
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
    rv_voice_conf conf = {};
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
    const int64_t play_code = play_voice(pdk, conf);
    if (play_code < 0) {
        return fail(state, clack, play_code);
    }
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
    const int64_t volume_code = set_volume(frame.pdk, state);
    if (volume_code != RV_OK) {
        warn_once(state, volume_code);
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
    const int64_t music_code = current == nullptr ? static_cast<int64_t>(RV_OK) : play_music(frame.pdk, state, current->music);
    if (music_code != RV_OK) {
        warn_once(state, music_code);
    }
    const int64_t stream_code = check_stream(frame.pdk, state);
    if (stream_code != RV_OK) {
        warn_once(state, stream_code);
    }
    return RV_OK;
}

int ecs_system_clack(rv_pdkkit::ecs_world &world, const rv_pdkkit::ecs_frame &frame)
{
    const std::vector<rv_pdkkit::ecs_id> listeners = holders_of<ecs_component_listener>(world);
    if (listeners.empty()) {
        return RV_OK;
    }
    if (listeners.size() > 1) {
        return RV_ERR_ALREADY;
    }
    ecs_component_listener *listener = rv_pdkkit::ecs_component_get<ecs_component_listener>(world, listeners[0]);
    const bool switched = !holders_of<ecs_component_flicker_switched>(world).empty();
    const bool in_factory = rv_pdkkit::ecs_component_get<ecs_component_zone_clack>(world, listener->zone) != nullptr;
    if (!switched || !in_factory) {
        return RV_OK;
    }
    const int64_t clack_code = play_clack(frame.pdk, listener->sound);
    if (clack_code != RV_OK) {
        warn_once(listener->sound, clack_code);
    }
    return RV_OK;
}

} // namespace example_cpp_pdkkit
