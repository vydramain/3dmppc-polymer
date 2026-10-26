// The programmed SPU's stream input: a ring of MONO int16 frames the drive fills and the
// mixer drains, with a level per channel. Every call after construction runs under the
// mixer's lock_ (rv_pcmixer.hpp:141); the class has no lock and no thread of its own.
#pragma once

#include <cstdint>
#include <vector>

namespace rv_3dmppc
{

// Ring size in mono frames: 64 KiB of int16, about 743 ms at 44100. A device constant
// like the SPU voice count, not a manifest field. The margin covers rAF delays on web.
constexpr int64_t RV_PCSTREAM_RING_FRAMES = 32768;

// The input state as one value, so a caller reads all of it under one lock.
struct rv_pcstream_report {
    int64_t consumed = 0;
    int64_t starved = 0;
    int64_t fill = 0;
    int64_t free = 0;
    bool drained = false;
};

class rv_pcstream
{
public:
    rv_pcstream();

    // Per-channel level in the rv_voice_conf units: RV_PCA_VOLUME_UNITY is unity,
    // <= 0 is silence. Kept across start() and clear().
    void set_volume(int16_t l, int16_t r);

    // Begin a stream: empty the ring, zero the counters, wait for data.
    void start();

    // No more data will come: an empty ring from here on is the end, not starvation.
    // Does nothing unless a stream waits, so drained() cannot hold for a stream never started.
    void end_input();

    // Stop: empty the ring and stop waiting for data. The counters stay for the report.
    void clear();

    // Copy up to `frames` mono frames from `src` and return how many were taken, which
    // is the smaller of `frames` and the free space. Takes nothing unless a stream waits.
    int64_t push(const int16_t *src, int64_t frames);

    // Add `frames` interleaved stereo frames into `acc`. Each frame in the ring is taken
    // and counted; an empty ring while a stream waits counts a starved frame instead.
    void mix(int32_t *acc, int64_t frames);

    int64_t fill_frames() const
    {
        return fill_;
    }

    int64_t free_frames() const
    {
        return RV_PCSTREAM_RING_FRAMES - fill_;
    }

    // Frames taken from the ring since the last start().
    int64_t consumed_frames() const
    {
        return consumed_;
    }

    // Frames the mixer wanted while a stream waited and the ring was empty.
    int64_t starved_frames() const
    {
        return starved_;
    }

    // end_input() was called and every frame has been taken.
    bool drained() const
    {
        return ended_ && fill_ == 0;
    }

    // Every query above at once; equal to calling them in a row under the same lock.
    rv_pcstream_report report() const;

private:
    std::vector<int16_t> ring_;
    int64_t head_ = 0; // index of the next frame to take
    int64_t fill_ = 0; // frames waiting in the ring
    int64_t consumed_ = 0;
    int64_t starved_ = 0;

    bool accepting_ = false; // a stream waits for data: push takes it, an empty ring starves
    bool ended_ = false;     // end_input() was called, and no start() or clear() since

    float gain_l_ = 1.0f;
    float gain_r_ = 1.0f;
};

} // namespace rv_3dmppc
