#include "example-cpp-pdkkit-sound.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
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

// A bar's asset is mus_<song>_NN.pcm, NN the bar's number from 01, at least RV_MUSIC_NUMBER_DIGITS wide.
constexpr const char *RV_MUSIC_PREFIX = "mus_";
constexpr const char *RV_MUSIC_SEPARATOR = "_";
constexpr const char *RV_MUSIC_SUFFIX = ".pcm";
constexpr std::size_t RV_MUSIC_NUMBER_DIGITS = 2;

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

// The asset of bar `number` (from 1) of song `name`.
std::string bar_asset(const char *name, int64_t number)
{
    std::string digits = std::to_string(number);
    if (digits.size() < RV_MUSIC_NUMBER_DIGITS) {
        digits.insert(0, RV_MUSIC_NUMBER_DIGITS - digits.size(), '0');
    }
    return std::string(RV_MUSIC_PREFIX) + name + RV_MUSIC_SEPARATOR + digits + RV_MUSIC_SUFFIX;
}

// Bar `bar` of the song, wrapping like the disc's bars: after the last one the first comes again.
std::string bar_name(const rv_music_song &song, int64_t bar)
{
    return bar_asset(song.name, bar % song.bars + 1);
}

// Makes music slot `slot` hold bar `name`. Callers write only a slot the music voice is not reading, so no voice
// is stopped here. The slot is unknown while the write runs, so a failed write is retried.
int64_t write_slot(rv_pdko *pdk, rv_sound_state &state, int slot, std::string name)
{
    if (std::strcmp(state.slot_name[slot], name.c_str()) == 0) {
        return RV_OK;
    }
    rv_ca *ca = rv_pdko_ca(pdk);
    rv_cd *cd = rv_pdko_cd(pdk);
    state.slot_name[slot][0] = '\0';
    const int64_t handle = rv_cd_asset_open(cd, name.c_str());
    if (handle < 0) {
        return fail(state, name.c_str(), handle);
    }
    const int64_t size = rv_cd_asset_size(cd, handle);
    if (size < 0) {
        return fail(state, name.c_str(), size);
    }
    // An empty bar or one larger than the slot is a bad disc: the asset is malformed for the slot.
    if (size == 0 || size > state.slot_bytes) {
        return fail(state, name.c_str(), RV_ERR_INVAL);
    }
    // The buffer lives only for this write: the state must stay trivially copyable, a component's working state. One
    // allocation per written bar, that is once every RV_SOUND_BAR_FRAMES frames.
    std::vector<uint8_t> scratch(static_cast<std::size_t>(size));
    // As Solid Maid (sm_sound.cpp:184-186): the return of read is the length of the bar.
    const int64_t read = rv_cd_asset_read(cd, handle, scratch.data(), size);
    if (read <= 0) {
        return fail(state, name.c_str(), RV_ERR_IO);
    }
    rv_sample sample = {};
    sample.data = scratch.data();
    sample.size = read;
    const int64_t write_code = rv_ca_sound_asset_write(ca, state.slot_addr[slot], &sample);
    if (write_code < 0) {
        return fail(state, name.c_str(), write_code);
    }
    set_name(state.slot_name[slot], name.c_str());
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

// Starts bar `bar` of the song when `play` is set; without `play` it only fills the slots (the first frame
// mid-bar waits for the next bar start). A running chain is restarted on the slot the voice is not reading.
int64_t start_chain(rv_pdko *pdk, rv_sound_state &state, const rv_music_song &song, int64_t bar, bool play)
{
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
        return fail(state, bar_name(song, bar).c_str(), play_code);
    }
    // The voice reads free_slot from here on, so the next bar is written only now. The chain is running from here:
    // a failed step is finished by arm_chain, not restarted, since a restart could write the slot being read.
    state.music_on = true;
    state.music_armed = false;
    state.music_slot = next_slot;
    state.music_bar = bar + 1;
    set_name(state.music_name, bar_name(song, bar + 1).c_str());
    // The chain plays this song and bar from here, so a retry after a failed arm sees no song change.
    set_name(state.song, song.name);
    state.bar = bar;
    return arm_chain(pdk, state);
}

// The voice reached its mark: the slot it has left is free and takes the bar after the one queued in the mark's slot.
// The mark moves to that slot only once it is armed, so a failed step is retried as the same step.
int64_t advance(rv_pdko *pdk, rv_sound_state &state, const rv_music_song &song)
{
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
    set_name(state.music_name, bar_name(song, queued).c_str());
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

// Counts the bars of song `name`: mus_<name>_01.pcm, _02, ... up to the first one the drive does not have. Raises
// `largest` to the biggest bar. Returns the count, or the first error with its bar as state.error_asset.
int64_t count_bars(rv_cd *cd, rv_sound_state &state, const std::string &name, int64_t &largest)
{
    int64_t bars = 0;
    for (;;) {
        const std::string asset = bar_asset(name.c_str(), bars + 1);
        const int64_t handle = rv_cd_asset_open(cd, asset.c_str());
        if (handle == RV_ERR_NOENT) {
            break;
        }
        if (handle < 0) {
            return fail(state, asset.c_str(), handle);
        }
        // Every bar name is kept in a field: a bar that does not fit refuses the song, named in full (the name fits).
        if (asset.size() >= static_cast<std::size_t>(rv_pdkkit::ecs_value_capacity)) {
            return fail(state, name.c_str(), RV_ERR_INVAL);
        }
        const int64_t size = rv_cd_asset_size(cd, handle);
        if (size < 0) {
            return fail(state, asset.c_str(), size);
        }
        if (size == 0) {
            return fail(state, asset.c_str(), RV_ERR_INVAL);
        }
        largest = std::max(largest, size);
        ++bars;
    }
    if (bars == 0) {
        return fail(state, bar_asset(name.c_str(), 1).c_str(), RV_ERR_NOENT);
    }
    return bars;
}

// The song rv_music_init measured under `name`, or null when there is none.
const rv_music_song *find_song(const rv_sound_state &state, const char *name)
{
    for (int64_t i = 0; i < state.song_count; ++i) {
        if (std::strcmp(state.songs[i].name, name) == 0) {
            return &state.songs[i];
        }
    }
    return nullptr;
}

} // namespace

int64_t rv_music_init(rv_pdko *pdk, const std::vector<std::string> &names, rv_sound_state &state)
{
    rv_cd *cd = rv_pdko_cd(pdk);
    rv_ca *ca = rv_pdko_ca(pdk);
    if (names.size() > RV_SOUND_SONG_CAPACITY) {
        return fail(state, "music songs", RV_ERR_NOMEM);
    }
    state.song_count = 0;

    // The slots are sized to the largest bar, measured off the disc: a re-recorded bar must not overflow them.
    int64_t largest = 0;
    for (const std::string &name : names) {
        if (name.size() >= static_cast<std::size_t>(rv_pdkkit::ecs_value_capacity)) {
            return fail(state, name.c_str(), RV_ERR_INVAL);
        }
        const int64_t bars = count_bars(cd, state, name, largest);
        if (bars < 0) {
            return bars;
        }
        rv_music_song &song = state.songs[state.song_count];
        set_name(song.name, name.c_str());
        song.bars = bars;
        ++state.song_count;
    }
    for (int slot = 0; slot < RV_SOUND_SLOTS; ++slot) {
        const int64_t addr = rv_ca_sound_asset_malloc(ca, largest);
        if (addr < 0) {
            free_slots(ca, state, slot);
            return fail(state, "music slot", addr);
        }
        state.slot_addr[slot] = addr;
    }
    state.slot_bytes = largest;
    return RV_OK;
}

int64_t rv_music_update(rv_pdko *pdk, const char *name, int64_t span_offset, rv_sound_state &state)
{
    const rv_music_song *song = find_song(state, name);
    if (song == nullptr) {
        return fail(state, "music song", RV_ERR_INVAL);
    }
    const bool offset_in_range = span_offset >= 0 && span_offset < RV_ROUTE_PERIOD_FRAMES;
    if (!offset_in_range) {
        return fail(state, "music offset", RV_ERR_INVAL);
    }
    const int64_t bar = span_offset / RV_SOUND_BAR_FRAMES;
    // Decided against the previous frame, before this frame takes its song and bar. A running chain whose frame was
    // not committed (its arm failed) is not a first frame: it is finished above, not started again.
    const bool first = state.frame < 0 && !state.music_on;
    const bool song_changed = first || std::strcmp(name, state.song) != 0;
    // A first frame mid-bar fills the slots and waits: the bar it is in is not played from its head.
    const bool silent = first && span_offset % RV_SOUND_BAR_FRAMES != 0;
    // A chain whose last step failed is finished first: its voice already plays, so a restart would be wrong.
    int64_t music_code = state.music_on && !state.music_armed ? arm_chain(pdk, state) : static_cast<int64_t>(RV_OK);
    if (music_code < 0) {
        return music_code;
    }
    // Before the chain runs it starts at a table bar start; once running, the voice's marks move it on.
    const bool starts = song_changed || (!state.music_on && bar != state.bar);
    if (starts) {
        music_code = start_chain(pdk, state, *song, bar, !silent);
    } else if (state.music_on) {
        bool reached = false;
        music_code = at_mark(pdk, state, reached);
        if (music_code == RV_OK && reached) {
            music_code = advance(pdk, state, *song);
        }
    }
    if (music_code < 0) {
        return music_code;
    }
    set_name(state.song, name);
    state.bar = bar;
    return RV_OK;
}

} // namespace example_cpp_pdkkit
