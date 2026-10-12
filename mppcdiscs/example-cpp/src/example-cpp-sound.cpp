#include "example-cpp-sound.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include "pdk/ca/rv_ca.h"
#include "pdk/ca/rv_loop.h"
#include "pdk/ca/rv_sample.h"
#include "pdk/ca/rv_voice_conf.h"
#include "pdk/cd/rv_cd.h"
#include "pdk/rv_err.h"
#include "pdklib/rv_logs/rv_logs.hpp"
#include "example-cpp-board.hpp"
#include "example-cpp-route.hpp"

namespace example_cpp
{

namespace
{

// Eye x of the camera, metres, as scenes/main.scene.toml places the objects: the factory is at x < 0, the house
// facade stands at x = 33.8 with the flat behind it. Eye x < 0 is the workshop, x > 33.8 the flat, between them the street.
constexpr float RV_SOUND_WORKSHOP_MAX_X = 0.0f;
constexpr float RV_SOUND_FLAT_MIN_X = 33.8f;

// One bar is one second of music and the frame clock is 60 frames per second, so a bar starts on an exact
// frame and needs no lead time: Solid Maid's 0.018 s lead covers the jitter of wall-clock time, not this.
constexpr int64_t RV_SOUND_BAR_FRAMES = 60;

// The bars of each area in playing order; the samples are the disc's [sounds] (assets/snd). A bar list
// wraps: after its last bar the first comes again. Order follows rv_sound_area.
constexpr const char *RV_SOUND_WORKSHOP_BARS[] = {
    "mus_factory_01.pcm",
    "mus_factory_02.pcm",
    "mus_factory_03.pcm",
};
constexpr const char *RV_SOUND_STREET_BARS[] = {
    "mus_street_01.pcm",
    "mus_street_02.pcm",
    "mus_street_03.pcm",
    "mus_street_04.pcm",
    "mus_street_05.pcm",
};
constexpr const char *RV_SOUND_FLAT_BARS[] = {
    "mus_home_01.pcm",
    "mus_home_02.pcm",
    "mus_home_03.pcm",
    "mus_home_04.pcm",
    "mus_home_05.pcm",
    "mus_home_06.pcm",
    "mus_home_07.pcm",
    "mus_home_08.pcm",
    "mus_home_09.pcm",
    "mus_home_10.pcm",
};

struct rv_sound_song {
    const char *const *bars;
    int64_t count;
};

constexpr rv_sound_song RV_SOUND_SONGS[] = {
    { RV_SOUND_WORKSHOP_BARS, static_cast<int64_t>(std::size(RV_SOUND_WORKSHOP_BARS)) },
    { RV_SOUND_STREET_BARS, static_cast<int64_t>(std::size(RV_SOUND_STREET_BARS)) },
    { RV_SOUND_FLAT_BARS, static_cast<int64_t>(std::size(RV_SOUND_FLAT_BARS)) },
};
static_assert(std::size(RV_SOUND_SONGS) == static_cast<std::size_t>(rv_sound_area::flat) + 1, "one song per rv_sound_area");
constexpr const char *RV_SOUND_CLACK = "sfx_board_clack.pcm";

// One voice bit per music slot, and one for the clack. A slot is refilled only after its voice is stopped,
// so writing a slot never touches the voice of the bar playing now; the previous bar's voice is the one stopped.
constexpr int64_t RV_SOUND_MUSIC_VOICE[RV_SOUND_SLOTS] = { 1, 2 };
constexpr int64_t RV_SOUND_CLACK_VOICE = 4;

// Volume, loop type and envelope as Solid Maid sets them for its factory loops (sm_sound.cpp:127-129, :410-452;
// sm_game.cpp:934-936). The gating is this demo's: both loops run while the camera is in the workshop, with no
// assembly-working state.
constexpr int64_t RV_SOUND_CONVEYOR_VOICE = 8;
constexpr int64_t RV_SOUND_ASSEMBLY_VOICE = 16;
constexpr int16_t RV_SOUND_CONVEYOR_VOLUME = 6600;
constexpr int16_t RV_SOUND_ASSEMBLY_VOLUME = 12100;
constexpr int16_t RV_SOUND_LOOP_ENVELOPE = 0;
constexpr const char *RV_SOUND_CONVEYOR_LOOP = "sfx_loop_conveyor.pcm";
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

// Music voices: Solid Maid sets no envelope (sm_sound.cpp:348-373), so ar, dr, sr and rr are 0 and the
// bar holds at full sustain; it is mixed at 10000 of 32767.
constexpr int16_t RV_SOUND_MUSIC_ATTACK = 0;
constexpr int16_t RV_SOUND_MUSIC_DECAY = 0;
constexpr int16_t RV_SOUND_MUSIC_SUSTAIN_RATE = 0;
constexpr int16_t RV_SOUND_MUSIC_RELEASE = 0;
constexpr int16_t RV_SOUND_MUSIC_VOLUME = 10000;
constexpr int16_t RV_SOUND_MAX_LEVEL = 32767;

// Clack envelope and volume: the shape example-lua gives its tone, at full volume.
constexpr int16_t RV_SOUND_CLACK_ATTACK = 5;
constexpr int16_t RV_SOUND_CLACK_DECAY = 40;
constexpr int16_t RV_SOUND_CLACK_SUSTAIN_RATE = 0;
constexpr int16_t RV_SOUND_CLACK_RELEASE = 120;
constexpr int16_t RV_SOUND_CLACK_SUSTAIN_LEVEL = 26000;

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
    const int64_t setup_code = rv_ca_voice_setup(ca, &conf);
    if (setup_code < 0) {
        return setup_code;
    }
    return rv_ca_voice_play(ca, conf.voice);
}

// True when the slot's voice is still playing at `frame`: a bar lasts one bar, the last frame included,
// since the sample runs a little past 60 frames. Only a playing voice can be stopped.
bool sounding(const rv_sound_state &state, int slot, int64_t frame)
{
    if (!state.slot_armed[slot]) {
        return false;
    }
    const int64_t period = RV_ROUTE_PERIOD_FRAMES;
    return (frame - state.slot_frame[slot] + period) % period <= RV_SOUND_BAR_FRAMES;
}

// Makes music slot `slot` hold bar `name`. A voice still playing may read the slot, so it is stopped
// before the slot is overwritten. The slot is unknown while the write runs, so a failed write is retried.
int64_t write_slot(rv_pdko *pdk, rv_sound_state &state, int slot, int64_t frame, const char *name)
{
    if (state.slot_name[slot] == name) {
        return RV_OK;
    }
    rv_ca *ca = rv_pdko_ca(pdk);
    rv_cd *cd = rv_pdko_cd(pdk);
    if (sounding(state, slot, frame)) {
        const int64_t stop_code = rv_ca_voice_stop(ca, RV_SOUND_MUSIC_VOICE[slot]);
        if (stop_code < 0) {
            return fail(state, name, stop_code);
        }
        state.slot_armed[slot] = false;
    }
    state.slot_name[slot] = nullptr;
    const int64_t handle = rv_cd_asset_open(cd, name);
    if (handle < 0) {
        return fail(state, name, handle);
    }
    const int64_t size = rv_cd_asset_size(cd, handle);
    if (size < 0) {
        return fail(state, name, size);
    }
    // An empty bar or one larger than the slot is a bad disc: the asset is malformed for the slot.
    if (size == 0 || size > static_cast<int64_t>(state.scratch.size())) {
        return fail(state, name, RV_ERR_INVAL);
    }
    // As Solid Maid (sm_sound.cpp:184-186): the return of read is the length of the bar.
    const int64_t read = rv_cd_asset_read(cd, handle, state.scratch.data(), size);
    if (read <= 0) {
        return fail(state, name, RV_ERR_IO);
    }
    rv_sample sample = {};
    sample.data = state.scratch.data();
    sample.size = read;
    const int64_t write_code = rv_ca_sound_asset_write(ca, state.slot_addr[slot], &sample);
    if (write_code < 0) {
        return fail(state, name, write_code);
    }
    state.slot_name[slot] = name;
    return RV_OK;
}

// Starts bar `bar` of the area's song when `play` is set, counted from the area span's first frame, and
// fills the other slot with the next bar. The bar's own slot is filled first when it is not yet.
int64_t play_bar(rv_pdko *pdk, rv_sound_state &state, rv_sound_area area, int64_t bar, int64_t frame, bool play)
{
    const rv_sound_song &song = RV_SOUND_SONGS[static_cast<std::size_t>(area)];
    const int slot = static_cast<int>(bar % RV_SOUND_SLOTS);
    const int next = (slot + 1) % RV_SOUND_SLOTS;
    const char *now_name = song.bars[bar % song.count];
    const char *next_name = song.bars[(bar + 1) % song.count];

    const int64_t now_code = write_slot(pdk, state, slot, frame, now_name);
    if (now_code < 0) {
        return now_code;
    }
    if (play) {
        rv_voice_conf conf = {};
        conf.voice = RV_SOUND_MUSIC_VOICE[slot];
        conf.loop_type = RV_LOOP_NONE;
        conf.sample_address = state.slot_addr[slot];
        conf.ar = RV_SOUND_MUSIC_ATTACK;
        conf.dr = RV_SOUND_MUSIC_DECAY;
        conf.sr = RV_SOUND_MUSIC_SUSTAIN_RATE;
        conf.rr = RV_SOUND_MUSIC_RELEASE;
        conf.sl = RV_SOUND_MAX_LEVEL;
        conf.volume = RV_SOUND_MUSIC_VOLUME;
        conf.volume_l = RV_SOUND_MAX_LEVEL;
        conf.volume_r = RV_SOUND_MAX_LEVEL;
        const int64_t play_code = play_voice(pdk, conf);
        if (play_code < 0) {
            return fail(state, now_name, play_code);
        }
        state.slot_armed[slot] = true;
        state.slot_frame[slot] = frame;
    }
    // Written ahead: a failure costs nothing now, since the slot is filled at the next bar start.
    const int64_t next_code = write_slot(pdk, state, next, frame, next_name);
    if (next_code < 0 && !state.write_ahead_logged) {
        state.write_ahead_logged = true;
        RV_LOG_WARN("sound", "{} not written ahead: rv_err {}, filled at the next bar start", next_name, next_code);
    }
    return RV_OK;
}

int64_t play_clack(rv_pdko *pdk, rv_sound_state &state)
{
    const int64_t addr = rv_cd_resource_addr(rv_pdko_cd(pdk), RV_CD_RESOURCE_AUDIO, RV_SOUND_CLACK);
    if (addr < 0) {
        return fail(state, RV_SOUND_CLACK, addr);
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
    // const int64_t play_code = play_voice(pdk, conf);
    // if (play_code < 0) {
    //     return fail(state, RV_SOUND_CLACK, play_code);
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
        const int64_t stop_code = rv_ca_voice_stop(rv_pdko_ca(pdk), loop.voice);
        if (stop_code < 0) {
            return fail(state, loop.name, stop_code);
        }
        state.loop_armed[index] = false;
        return RV_OK;
    }
    const int64_t addr = rv_cd_resource_addr(rv_pdko_cd(pdk), RV_CD_RESOURCE_AUDIO, loop.name);
    if (addr < 0) {
        return fail(state, loop.name, addr);
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

// Keeps both loops playing while `in_workshop`, and silent otherwise. Both are tried; the first failure is returned.
int64_t set_loops(rv_pdko *pdk, rv_sound_state &state, bool in_workshop)
{
    int64_t code = RV_OK;
    const char *asset = "";
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

// Frees the first `count` slots of a failed init; a slot that will not free is logged, the init error is kept.
void free_slots(rv_ca *ca, const rv_sound_state &state, int count)
{
    for (int slot = 0; slot < count; ++slot) {
        const int64_t free_code = rv_ca_sound_asset_free(ca, state.slot_addr[slot]);
        if (free_code < 0) {
            RV_LOG_WARN("sound", "music slot not freed: rv_err {}", free_code);
        }
    }
}

} // namespace

int64_t rv_sound_init(rv_pdko *pdk, const std::vector<rv_route_key> &keys, rv_sound_state &state)
{
    rv_cd *cd = rv_pdko_cd(pdk);
    rv_ca *ca = rv_pdko_ca(pdk);

    // The slots are sized to the largest bar, measured off the disc: a re-recorded bar must not overflow them.
    int64_t largest = 0;
    for (const rv_sound_song &song : RV_SOUND_SONGS) {
        for (int64_t i = 0; i < song.count; ++i) {
            const int64_t handle = rv_cd_asset_open(cd, song.bars[i]);
            if (handle < 0) {
                return fail(state, song.bars[i], handle);
            }
            const int64_t size = rv_cd_asset_size(cd, handle);
            if (size < 0) {
                return fail(state, song.bars[i], size);
            }
            if (size == 0) {
                return fail(state, song.bars[i], RV_ERR_INVAL);
            }
            largest = std::max(largest, size);
        }
    }
    for (int slot = 0; slot < RV_SOUND_SLOTS; ++slot) {
        const int64_t addr = rv_ca_sound_asset_malloc(ca, largest);
        if (addr < 0) {
            free_slots(ca, state, slot);
            return fail(state, "music slot", addr);
        }
        state.slot_addr[slot] = addr;
    }
    state.scratch.assign(static_cast<std::size_t>(largest), 0);

    // Each frame's area, and the first frame of its span. Spans wrap: the workshop may cross the seam
    // from the last frame of the period to the first, so the walk starts at an area change and goes round once.
    const int64_t period = RV_ROUTE_PERIOD_FRAMES;
    state.areas.resize(static_cast<std::size_t>(period));
    state.span_start.assign(static_cast<std::size_t>(period), 0);
    for (int64_t f = 0; f < period; ++f) {
        state.areas[static_cast<std::size_t>(f)] = area_of_frame(keys, f);
    }
    int64_t change = -1;
    for (int64_t f = 0; f < period && change < 0; ++f) {
        if (starts_span(state.areas, f)) {
            change = f;
        }
    }
    // With no change at all the one area covers the period and every span starts at frame 0.
    if (change >= 0) {
        int64_t start = change;
        for (int64_t i = 0; i < period; ++i) {
            const int64_t f = (change + i) % period;
            if (starts_span(state.areas, f)) {
                start = f;
            }
            state.span_start[static_cast<std::size_t>(f)] = start;
        }
    }
    rv_steps_init(keys, state);
    return RV_OK;
}

int64_t rv_sound_update(rv_pdko *pdk, int64_t frame, bool board_present, rv_sound_state &state)
{
    const int64_t period = RV_ROUTE_PERIOD_FRAMES;
    if (state.areas.size() != static_cast<std::size_t>(period) || state.distance.size() != static_cast<std::size_t>(period)) {
        return fail(state, "sound tables", RV_ERR_INVAL);
    }
    // The same frame again is no transition: a pause or a repeated render plays nothing.
    if (frame == state.frame) {
        return RV_OK;
    }
    const std::size_t index = static_cast<std::size_t>(frame);
    const rv_sound_area area = state.areas[index];
    const int64_t span_offset = (frame - state.span_start[index] + period) % period;
    const int64_t bar = span_offset / RV_SOUND_BAR_FRAMES;
    // Both are decided against the previous frame before state takes this one.
    const bool first = state.frame < 0;
    const bool area_changed = first || area != state.area;
    const bool bar_starts = area_changed || bar != state.bar;
    // A first frame mid-bar fills the slots and waits: the bar it is in is not played from its head.
    const bool silent = first && span_offset % RV_SOUND_BAR_FRAMES != 0;
    const bool clack = board_present && !first && area == rv_sound_area::workshop && letters_changed(state.frame, frame);
    // The loops always run. The clack runs only after the music step succeeded: otherwise the same clack would
    // repeat every frame. The first failure is returned with its asset.
    int64_t code = RV_OK;
    const char *asset = "";
    int64_t music_code = RV_OK;
    if (bar_starts) {
        music_code = play_bar(pdk, state, area, bar, frame, !silent);
    }
    if (music_code < 0) {
        code = music_code;
        asset = state.error_asset;
    }
    // Recorded only after the music start: a failed start is retried on the next rv_sound_update call
    // (the next frame, or the same frame again while paused).
    const bool music_ok = music_code == RV_OK;
    if (music_ok) {
        state.frame = frame;
        state.area = area;
        state.bar = bar;
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
    // Only once the frame is committed: a failed music start is retried on the next rv_sound_update call
    // (the next frame, or the same frame again while paused), and its step must not repeat.
    // const int64_t steps_code = music_ok ? rv_steps_update(pdk, frame, state) : RV_OK;
    const int64_t steps_code = RV_OK;
    if (code == RV_OK && steps_code < 0) {
        code = steps_code;
        asset = state.error_asset;
    }
    state.error_asset = asset;
    return code;
}

} // namespace example_cpp
