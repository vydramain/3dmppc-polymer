#include "rv_pconsole/ca/rv_pcstream.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "rv_pconsole/ca/rv_pcca.hpp"
#include "rv_pconsole/ca/rv_pcvoice.hpp"

namespace rv_3dmppc
{

namespace
{

// The same rule as gain_of in rv_pcvoice.cpp, repeated here rather than shared: the
// two inputs are independent.
float gain_of(int16_t volume)
{
    if (volume <= 0) {
        return 0.0f;
    }
    const float g = static_cast<float>(volume) / RV_PCA_VOLUME_UNITY;
    return g > 1.0f ? 1.0f : g;
}

} // namespace

rv_pcstream::rv_pcstream()
    : ring_(static_cast<std::size_t>(RV_PCSTREAM_RING_FRAMES))
{
}

void rv_pcstream::set_volume(int16_t l, int16_t r)
{
    gain_l_ = gain_of(l);
    gain_r_ = gain_of(r);
}

void rv_pcstream::start()
{
    head_ = 0;
    fill_ = 0;
    consumed_ = 0;
    starved_ = 0;
    accepting_ = true;
    ended_ = false;
}

void rv_pcstream::end_input()
{
    if (!accepting_) {
        return;
    }

    accepting_ = false;
    ended_ = true;
}

void rv_pcstream::clear()
{
    head_ = 0;
    fill_ = 0;
    accepting_ = false;
    ended_ = false;
}

int64_t rv_pcstream::push(const int16_t *src, int64_t frames)
{
    if (!src || frames <= 0 || !accepting_) {
        return 0;
    }

    const int64_t take = std::min(frames, free_frames());
    if (take <= 0) {
        return 0;
    }

    // Copy to the ring's end first, then wrap: at most two copies, however the ring is placed.
    int16_t *const ring = ring_.data();
    const int64_t write = (head_ + fill_) % RV_PCSTREAM_RING_FRAMES;
    const int64_t first = std::min(take, RV_PCSTREAM_RING_FRAMES - write);
    std::memcpy(ring + write, src, static_cast<std::size_t>(first) * sizeof(int16_t));
    std::memcpy(ring, src + first, static_cast<std::size_t>(take - first) * sizeof(int16_t));

    fill_ += take;
    return take;
}

void rv_pcstream::mix(int32_t *acc, int64_t frames)
{
    if (!acc || frames <= 0) {
        return;
    }

    const int16_t *const ring = ring_.data();
    for (int64_t i = 0; i < frames; ++i) {
        if (fill_ == 0) {
            // Silence costs nothing unless a stream waits: after end_input() an empty ring is the end.
            if (accepting_) {
                ++starved_;
            }
            continue;
        }

        const float sample = static_cast<float>(ring[head_]);
        acc[RV_PCCA_PCM_CHANNELS * i] += static_cast<int32_t>(sample * gain_l_);
        acc[RV_PCCA_PCM_CHANNELS * i + 1] += static_cast<int32_t>(sample * gain_r_);

        head_ = (head_ + 1) % RV_PCSTREAM_RING_FRAMES;
        --fill_;
        ++consumed_;
    }
}

rv_pcstream_report rv_pcstream::report() const
{
    return { consumed_, starved_, fill_, free_frames(), drained() };
}

} // namespace rv_3dmppc
