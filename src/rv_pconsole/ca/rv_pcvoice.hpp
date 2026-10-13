// One voice of the console's SPU: a read head walking a sample in sound RAM, an
// ADSR envelope shaping it, and two channel gains spreading the result across
// the stereo field.
//
// The voice is MONO in and STEREO out. That is the PSX SPU model and the reason
// rv_sample carries no format fields: sound RAM holds raw S16LE mono frames at
// RV_PCA_SAMPLE_RATE, and where a voice sits in the stereo image is a property
// of the VOICE (volume_l / volume_r), not of the sample. Two voices can play the
// same sample from opposite sides at once.
//
// Knows nothing about any platform, about the mixer that owns it, or about the
// pool the bytes live in - it borrows a pointer and a frame count and does
// arithmetic.
#pragma once

#include <cstdint>

#include "pdk/ca/rv_loop.h"
#include "pdk/ca/rv_voice_conf.h"

namespace rv_3dmppc
{

// Everything in sound RAM is recorded at this rate, and the platform plays PCM
// at the same rate. See rv_pcmixer.hpp for why there is no resampler.
constexpr int64_t RV_PCA_SAMPLE_RATE = 44100;

// Number of bytes one mono frame of sound RAM occupies (S16LE).
constexpr int64_t RV_PCA_FRAME_BYTES = 2;

// Full-scale value of an rv_voice_conf volume field: `volume == 32767` is unity
// gain. The contract gives the fields no units, so the console defines them -
// and defines them so that ONE voice at unity, playing a full-scale sample, is
// exactly full scale on the output and never clips.
constexpr float RV_PCA_VOLUME_UNITY = 32767.0f;

// The four phases of the envelope, plus the resting state. A voice is BUSY
// (rv_ca::voice_status) in every phase except idle - including release, which is
// why a stopped voice keeps its sound-RAM region reserved until its tail decays.
enum class rv_pcvoice_phase {
    idle = 0,
    attack,
    decay,
    sustain,
    release,
};

// Repeat lets the read head jump from one sound-RAM region into another at its end,
// so a song can run across two regions with no gap. The mark is a flag raised when
// the read head reads one chosen frame, for the sequencer to poll.
class rv_pcvoice
{
public:
    // Load a config. `data` points at the region's first byte in sound RAM and
    // `frames` is how many S16LE frames were uploaded into it; `addr` is the
    // opaque address the disc named, kept so sound_asset_free can tell whether
    // this voice is still reading from a region.
    //
    // Arming does not make a sound: nothing is audible until play().
    void setup(const rv_voice_conf &conf, const uint8_t *data, int64_t frames, int64_t addr);

    // Key-on. Rewinds the read head and restarts the envelope from silence, so
    // re-triggering a sounding voice is a legal retrigger rather than a click.
    void play();

    // Key-off: hand the envelope to its release ramp. The voice stays busy until
    // the tail reaches zero - that is the whole point of having a release.
    void stop();

    // Forget the config entirely: silent, unarmed, holding no pointer. Used when
    // the region a voice was armed with is released back to the pool.
    void disarm();

    // Set the repeat: at the end of the current region under RV_LOOP_FOREVER the read
    // head jumps to `offset` frames into this region. Called after play(), which
    // resets the repeat to the start region. A negative offset counts as 0.
    void set_repeat(int64_t addr, const uint8_t *data, int64_t frames, int64_t offset);

    // Raise marked() when the read head reads frame `offset` of the region at `addr`,
    // including a frame reached by a repeat jump. Setting or clearing resets marked().
    void set_mark(int64_t addr, int64_t offset);
    void clear_mark();

    // Did the read head pass the end of its current region since the last play()?
    bool ended() const
    {
        return ended_;
    }

    // Has the mark frame been read since it was set?
    bool marked() const
    {
        return marked_;
    }

    // Set the frame length of every region of this voice that starts at `addr`. The
    // caller guarantees `frames` does not exceed the region's allocated size; the
    // voice does not check it. Length 0 retires a voice that is reading that region.
    void resize_region(int64_t addr, int64_t frames);

    // Has a config ever been loaded? rv_ca::voice_play must refuse a voice that
    // was never set up, and this is the question it asks.
    bool armed() const
    {
        return armed_;
    }

    // Is the voice making sound (any phase but idle)?
    bool busy() const
    {
        return phase_ != rv_pcvoice_phase::idle;
    }

    // The sound-RAM address this voice was armed with (the start region), or 0 when
    // not configured (never armed, or after disarm).
    int64_t region() const
    {
        return start_.addr;
    }

    // The region being read now, and the repeat region. Only meaningful while busy().
    int64_t current_region() const
    {
        return cur_.addr;
    }

    int64_t repeat_region() const
    {
        return repeat_.addr;
    }

    // The marked region's address, or 0 when no mark is set.
    int64_t mark_region() const
    {
        return mark_on_ ? mark_region_ : 0;
    }

    // Accumulate `frames` stereo frames into `out` (interleaved L, R), advancing
    // the read head and the envelope one step per frame. ADDS to the buffer and
    // never writes zeroes into it - summing is the mixer's whole algorithm, see
    // the saturation theorem in rv_pcmixer.hpp.
    void mix(int32_t *out, int64_t frames);

private:
    // Move to `phase`, recomputing the ramp that leads out of it. A segment with
    // a non-positive time is not a slow ramp but an INSTANT one: the level jumps
    // to the segment's target and the next step falls through to the phase after
    // it. Only sustain reads a zero time as "hold forever".
    void enter(rv_pcvoice_phase phase);

    // One envelope step; the phase automaton lives here.
    void advance();

    // Per-frame delta that walks `from` to `to` in `ms` milliseconds, or 0 when
    // the segment is instant (or already at its target).
    static float ramp(float from, float to, int16_t ms);

    // Decode frame `index` of the region. Reads the two bytes explicitly instead
    // of casting the pool to int16_t*: sound RAM is a byte pool, S16LE is the
    // documented on-disc format, and doing the shift by hand keeps the decode
    // correct on a big-endian host and legal under the aliasing rules.
    int32_t frame_at(int64_t index) const;

    // Jump to the repeat region at its repeat offset. False when there is nothing to
    // jump to, and the caller retires the voice.
    bool wrap_to_repeat();

    // A region as the voice sees it: its address, its first byte, and its length in frames.
    struct sram_region {
        int64_t addr = 0;
        const uint8_t *data = nullptr;
        int64_t frames = 0;
    };

    bool armed_ = false;

    // Borrowed, NOT owned: the bytes live in the rv_pcpool inside rv_pcca. The
    // pool's storage is allocated once in its constructor and never resized, so
    // these pointers stay valid for as long as the region is not freed - and a
    // region a voice is reading from cannot be freed (rv_ca RV_ERR_BUSY).
    sram_region start_;  // what setup() named; play() rewinds to it
    sram_region cur_;    // the region being read now
    sram_region repeat_; // where the read head jumps at the end of cur_ under RV_LOOP_FOREVER
    int64_t repeat_offset_ = 0;
    int64_t position_ = 0;
    rv_loop loop_ = RV_LOOP_NONE;

    bool ended_ = false;
    bool mark_on_ = false;
    int64_t mark_region_ = 0;
    int64_t mark_offset_ = 0;
    bool marked_ = false;

    rv_pcvoice_phase phase_ = rv_pcvoice_phase::idle;
    float envelope_ = 0.0f; // 0 .. 1, the current gain of the envelope
    float step_ = 0.0f;     // per-frame delta of the active ramp
    float target_ = 0.0f;   // level the active ramp is walking towards

    int16_t attack_ms_ = 0;
    int16_t decay_ms_ = 0;
    int16_t sustain_ms_ = 0; // 0 = hold the sustain level indefinitely
    int16_t release_ms_ = 0;
    float sustain_level_ = 1.0f; // the knee between decay and release

    float gain_l_ = 0.0f;
    float gain_r_ = 0.0f;
};

} // namespace rv_3dmppc
