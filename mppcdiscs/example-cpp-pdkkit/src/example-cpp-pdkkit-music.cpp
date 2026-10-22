#include "example-cpp-pdkkit-sound.hpp"

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
#include "example-cpp-pdkkit-route.hpp"

namespace example_cpp_pdkkit
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

// One music voice plays the bars of both slots: the sound module moves it on at each bar's end by the repeat, and
// the mark tells this frame it arrived. The partner voice of the old two-voice pattern is no longer used by music.
constexpr int64_t RV_SOUND_MUSIC_VOICE = 1;

// Music voices: Solid Maid sets no envelope (sm_sound.cpp:348-373), so ar, dr, sr and rr are 0 and the
// bar holds at full sustain; it is mixed at 10000 of 32767.
constexpr int16_t RV_SOUND_MUSIC_ATTACK = 0;
constexpr int16_t RV_SOUND_MUSIC_DECAY = 0;
constexpr int16_t RV_SOUND_MUSIC_SUSTAIN_RATE = 0;
constexpr int16_t RV_SOUND_MUSIC_RELEASE = 0;
constexpr int16_t RV_SOUND_MUSIC_VOLUME = 10000;

// The other slot of the two-slot cycle.
int other_slot(int slot)
{
    return (slot + 1) % RV_SOUND_SLOTS;
}

// Bar `bar` of the song, wrapping like the bar list.
const char *bar_name(const rv_sound_song &song, int64_t bar)
{
    return song.bars[bar % song.count];
}

// Makes music slot `slot` hold bar `name`. Callers write only a slot the music voice is not reading, so no voice
// is stopped here. The slot is unknown while the write runs, so a failed write is retried.
int64_t write_slot(rv_pdko *pdk, rv_sound_state &state, int slot, const char *name)
{
    if (state.slot_name[slot] == name) {
        return RV_OK;
    }
    rv_ca *ca = rv_pdko_ca(pdk);
    rv_cd *cd = rv_pdko_cd(pdk);
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

// Reads whether the music voice has reached its mark: then it reads the mark's slot, else the other slot.
int64_t at_mark(rv_pdko *pdk, rv_sound_state &state, bool &reached)
{
    const int64_t marked = rv_ca_voice_marked(rv_pdko_ca(pdk), RV_SOUND_MUSIC_VOICE);
    if (marked < 0) {
        return fail(state, "music mark", marked);
    }
    reached = (marked & RV_SOUND_MUSIC_VOICE) != 0;
    return RV_OK;
}

// Sets the voice to go on into `slot` when its bar ends, and marks that slot's start so the next frame sees the
// voice arrive. Both are set again on a retry, so a failed call is repeated whole.
int64_t arm(rv_pdko *pdk, rv_sound_state &state, int slot)
{
    rv_ca *ca = rv_pdko_ca(pdk);
    const char *name = state.slot_name[slot];
    const int64_t addr = state.slot_addr[slot];
    const int64_t repeat_code = rv_ca_voice_repeat(ca, RV_SOUND_MUSIC_VOICE, addr);
    if (repeat_code < 0) {
        return fail(state, name, repeat_code);
    }
    const int64_t mark_code = rv_ca_voice_mark(ca, RV_SOUND_MUSIC_VOICE, addr);
    if (mark_code < 0) {
        return fail(state, name, mark_code);
    }
    return RV_OK;
}

// Writes the queued bar into music_slot and arms its mark: the last step of a chain start. Retried until both succeed.
int64_t arm_chain(rv_pdko *pdk, rv_sound_state &state)
{
    const int64_t write_code = write_slot(pdk, state, state.music_slot, state.music_name);
    if (write_code < 0) {
        return write_code;
    }
    const int64_t arm_code = arm(pdk, state, state.music_slot);
    if (arm_code < 0) {
        return arm_code;
    }
    state.music_armed = true;
    return RV_OK;
}

// Starts bar `bar` of the area's song when `play` is set; without `play` it only fills the slots (the first frame
// mid-bar waits for the next bar start). A running chain is restarted on the slot the voice is not reading.
int64_t start_chain(rv_pdko *pdk, rv_sound_state &state, rv_sound_area area, int64_t bar, bool play)
{
    const rv_sound_song &song = RV_SOUND_SONGS[static_cast<std::size_t>(area)];
    int free_slot = static_cast<int>(bar % RV_SOUND_SLOTS);
    if (state.music_on) {
        bool reached = false;
        const int64_t mark_code = at_mark(pdk, state, reached);
        if (mark_code < 0) {
            return mark_code;
        }
        free_slot = reached ? other_slot(state.music_slot) : state.music_slot;
    }
    const int next_slot = other_slot(free_slot);
    const int64_t now_code = write_slot(pdk, state, free_slot, bar_name(song, bar));
    if (now_code < 0) {
        return now_code;
    }
    if (!play) {
        return write_slot(pdk, state, next_slot, bar_name(song, bar + 1));
    }
    rv_voice_conf conf = {};
    conf.voice = RV_SOUND_MUSIC_VOICE;
    conf.loop_type = RV_LOOP_FOREVER;
    conf.sample_address = state.slot_addr[free_slot];
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
        return fail(state, bar_name(song, bar), play_code);
    }
    // The voice reads free_slot from here on, so the next bar is written only now. The chain is running from here:
    // a failed step is finished by arm_chain, not restarted, since a restart could write the slot being read.
    state.music_on = true;
    state.music_armed = false;
    state.music_slot = next_slot;
    state.music_bar = bar + 1;
    state.music_name = bar_name(song, bar + 1);
    // The chain plays this area and bar from here, so a retry after a failed arm sees no area change.
    state.area = area;
    state.bar = bar;
    return arm_chain(pdk, state);
}

// The voice reached its mark: the slot it has left is free and takes the bar after the one queued in the mark's slot.
// The mark moves to that slot only once it is armed, so a failed step is retried as the same step.
int64_t advance(rv_pdko *pdk, rv_sound_state &state)
{
    const rv_sound_song &song = RV_SOUND_SONGS[static_cast<std::size_t>(state.area)];
    const int free_slot = other_slot(state.music_slot);
    const int64_t queued = state.music_bar + 1;
    const int64_t write_code = write_slot(pdk, state, free_slot, bar_name(song, queued));
    if (write_code < 0) {
        return write_code;
    }
    const int64_t arm_code = arm(pdk, state, free_slot);
    if (arm_code < 0) {
        return arm_code;
    }
    state.music_slot = free_slot;
    state.music_bar = queued;
    state.music_name = bar_name(song, queued);
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
    // Decided against the previous frame, before this frame takes its area and bar. A running chain whose frame was
    // not committed (its arm failed) is not a first frame: it is finished above, not started again.
    const bool first = state.frame < 0 && !state.music_on;
    const bool area_changed = first || area != state.area;
    // A first frame mid-bar fills the slots and waits: the bar it is in is not played from its head.
    const bool silent = first && span_offset % RV_SOUND_BAR_FRAMES != 0;
    // A chain whose last step failed is finished first: its voice already plays, so a restart would be wrong.
    int64_t music_code = state.music_on && !state.music_armed ? arm_chain(pdk, state) : static_cast<int64_t>(RV_OK);
    if (music_code < 0) {
        return music_code;
    }
    // Before the chain runs it starts at a table bar start; once running, the voice's marks move it on.
    const bool starts = area_changed || (!state.music_on && bar != state.bar);
    if (starts) {
        music_code = start_chain(pdk, state, area, bar, !silent);
    } else if (state.music_on) {
        bool reached = false;
        music_code = at_mark(pdk, state, reached);
        if (music_code == RV_OK && reached) {
            music_code = advance(pdk, state);
        }
    }
    if (music_code < 0) {
        return music_code;
    }
    state.area = area;
    state.bar = bar;
    return RV_OK;
}

} // namespace example_cpp_pdkkit
