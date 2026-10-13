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
#include "example-cpp-route.hpp"

namespace example_cpp
{

namespace
{

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

// One voice bit per music slot. A slot is refilled only after its voice is stopped,
// so writing a slot never touches the voice of the bar playing now; the previous bar's voice is the one stopped.
constexpr int64_t RV_SOUND_MUSIC_VOICE[RV_SOUND_SLOTS] = { 1, 2 };

// Music voices: Solid Maid sets no envelope (sm_sound.cpp:348-373), so ar, dr, sr and rr are 0 and the
// bar holds at full sustain; it is mixed at 10000 of 32767.
constexpr int16_t RV_SOUND_MUSIC_ATTACK = 0;
constexpr int16_t RV_SOUND_MUSIC_DECAY = 0;
constexpr int16_t RV_SOUND_MUSIC_SUSTAIN_RATE = 0;
constexpr int16_t RV_SOUND_MUSIC_RELEASE = 0;
constexpr int16_t RV_SOUND_MUSIC_VOLUME = 10000;

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

int64_t rv_music_init(rv_pdko *pdk, rv_sound_state &state)
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
    return RV_OK;
}

int64_t rv_music_update(rv_pdko *pdk, int64_t frame, rv_sound_state &state)
{
    const int64_t period = RV_ROUTE_PERIOD_FRAMES;
    const std::size_t index = static_cast<std::size_t>(frame);
    const rv_sound_area area = state.areas[index];
    const int64_t span_offset = (frame - state.span_start[index] + period) % period;
    const int64_t bar = span_offset / RV_SOUND_BAR_FRAMES;
    // Decided against the previous frame, before this frame takes its area and bar.
    const bool first = state.frame < 0;
    const bool area_changed = first || area != state.area;
    const bool bar_starts = area_changed || bar != state.bar;
    // A first frame mid-bar fills the slots and waits: the bar it is in is not played from its head.
    const bool silent = first && span_offset % RV_SOUND_BAR_FRAMES != 0;
    const int64_t music_code = bar_starts ? play_bar(pdk, state, area, bar, frame, !silent) : RV_OK;
    if (music_code < 0) {
        return music_code;
    }
    state.area = area;
    state.bar = bar;
    return RV_OK;
}

} // namespace example_cpp
