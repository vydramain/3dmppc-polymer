#include "rv_pconsole/ca/rv_pcmixer.hpp"

#include <algorithm>

#include "pdk/rv_err.h"
#include "rv_pconsole/ca/rv_pcca.hpp"

namespace rv_3dmppc
{

// rv_pcstream::mix strides by RV_PCCA_PCM_CHANNELS over an accumulator laid out by
// RV_PCMIXER_CHANNELS: the two must agree or the stream lands on the wrong channels.
static_assert(RV_PCMIXER_CHANNELS == RV_PCCA_PCM_CHANNELS, "mixer and PCM output channel counts differ");

namespace
{

constexpr int32_t RV_PCMIXER_S16_MAX = 32767;
constexpr int32_t RV_PCMIXER_S16_MIN = -32768;

int16_t saturate(int32_t value)
{
    if (value > RV_PCMIXER_S16_MAX) {
        return static_cast<int16_t>(RV_PCMIXER_S16_MAX);
    }
    if (value < RV_PCMIXER_S16_MIN) {
        return static_cast<int16_t>(RV_PCMIXER_S16_MIN);
    }
    return static_cast<int16_t>(value);
}

bool mask_has(int64_t mask, int64_t index)
{
    return (mask & (static_cast<int64_t>(1) << index)) != 0;
}

} // namespace

rv_pcmixer::rv_pcmixer(int64_t voice_count)
    : voices_(static_cast<std::size_t>(voice_count > 0 ? voice_count : 0))
    , accumulator_(static_cast<std::size_t>(RV_PCMIXER_BLOCK_FRAMES * RV_PCMIXER_CHANNELS), 0)
{
}

void rv_pcmixer::set_muted(bool muted)
{
    std::lock_guard<std::mutex> guard(lock_);
    muted_ = muted;
}

void rv_pcmixer::setup(int64_t mask, const rv_voice_conf &conf, const uint8_t *data, int64_t frames, int64_t addr)
{
    std::lock_guard<std::mutex> guard(lock_);
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (mask_has(mask, static_cast<int64_t>(i))) {
            voices_[i].setup(conf, data, frames, addr);
        }
    }
}

// PATTERN: validate-then-apply. The whole mask is checked before a single voice
// moves, so a call naming one unarmed voice out of eight leaves all eight
// exactly as they were. Half-applied hardware commands are the kind of bug that
// only shows up as an occasional stuck note.
int rv_pcmixer::for_each_locked(int64_t mask, bool require_armed, void (rv_pcvoice::*action)())
{
    if (require_armed && !all_armed_locked(mask)) {
        return RV_ERR_INVAL;
    }

    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (mask_has(mask, static_cast<int64_t>(i))) {
            (voices_[i].*action)();
        }
    }
    return RV_OK;
}

int rv_pcmixer::play(int64_t mask)
{
    std::lock_guard<std::mutex> guard(lock_);
    return for_each_locked(mask, true, &rv_pcvoice::play);
}

int rv_pcmixer::stop(int64_t mask)
{
    std::lock_guard<std::mutex> guard(lock_);
    return for_each_locked(mask, true, &rv_pcvoice::stop);
}

bool rv_pcmixer::armed(int64_t mask) const
{
    std::lock_guard<std::mutex> guard(lock_);
    return all_armed_locked(mask);
}

int64_t rv_pcmixer::status(int64_t mask) const
{
    std::lock_guard<std::mutex> guard(lock_);

    int64_t busy = 0;
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        const int64_t bit = static_cast<int64_t>(i);
        if (mask_has(mask, bit) && voices_[i].busy()) {
            busy |= static_cast<int64_t>(1) << bit;
        }
    }
    return busy;
}

bool rv_pcmixer::all_armed_locked(int64_t mask) const
{
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (mask_has(mask, static_cast<int64_t>(i)) && !voices_[i].armed()) {
            return false;
        }
    }
    return true;
}

// Validate the whole mask first: one unarmed voice must not leave it half retargeted.
template <typename Action>
int rv_pcmixer::apply_armed_locked(int64_t mask, Action action)
{
    if (!all_armed_locked(mask)) {
        return RV_ERR_INVAL;
    }

    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (mask_has(mask, static_cast<int64_t>(i))) {
            action(voices_[i]);
        }
    }
    return RV_OK;
}

int64_t rv_pcmixer::flags_locked(int64_t mask, bool (rv_pcvoice::*flag)() const) const
{
    if (!all_armed_locked(mask)) {
        return static_cast<int64_t>(RV_ERR_INVAL);
    }

    int64_t raised = 0;
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        const int64_t bit = static_cast<int64_t>(i);
        if (mask_has(mask, bit) && (voices_[i].*flag)()) {
            raised |= static_cast<int64_t>(1) << bit;
        }
    }
    return raised;
}

int rv_pcmixer::repeat(int64_t mask, int64_t addr, const uint8_t *data, int64_t frames, int64_t offset)
{
    std::lock_guard<std::mutex> guard(lock_);
    return apply_armed_locked(mask, [addr, data, frames, offset](rv_pcvoice &voice) {
        voice.set_repeat(addr, data, frames, offset);
    });
}

int rv_pcmixer::mark(int64_t mask, int64_t addr, int64_t offset)
{
    std::lock_guard<std::mutex> guard(lock_);
    if (addr == 0) {
        return apply_armed_locked(mask, [](rv_pcvoice &voice) {
            voice.clear_mark();
        });
    }
    return apply_armed_locked(mask, [addr, offset](rv_pcvoice &voice) {
        voice.set_mark(addr, offset);
    });
}

int64_t rv_pcmixer::ended(int64_t mask) const
{
    std::lock_guard<std::mutex> guard(lock_);
    return flags_locked(mask, &rv_pcvoice::ended);
}

int64_t rv_pcmixer::marked(int64_t mask) const
{
    std::lock_guard<std::mutex> guard(lock_);
    return flags_locked(mask, &rv_pcvoice::marked);
}

std::unique_lock<std::mutex> rv_pcmixer::acquire()
{
    return std::unique_lock<std::mutex>(lock_);
}

bool rv_pcmixer::region_busy_locked(int64_t addr) const
{
    // Address 0 is "no region", and an idle mark reads as 0: without this guard
    // every voice without a mark would report the region busy.
    if (addr == 0) {
        return false;
    }

    for (const rv_pcvoice &voice : voices_) {
        if (voice.mark_region() == addr) {
            return true;
        }
        if (voice.busy() && (voice.region() == addr || voice.current_region() == addr || voice.repeat_region() == addr)) {
            return true;
        }
    }
    return false;
}

void rv_pcmixer::disarm_region_locked(int64_t addr)
{
    for (rv_pcvoice &voice : voices_) {
        if (voice.region() == addr) {
            voice.disarm();
        }
    }
}

void rv_pcmixer::resize_region_locked(int64_t addr, int64_t frames)
{
    for (rv_pcvoice &voice : voices_) {
        voice.resize_region(addr, frames);
    }
}

void rv_pcmixer::stream_volume(int16_t l, int16_t r)
{
    std::lock_guard<std::mutex> guard(lock_);
    stream_.set_volume(l, r);
}

void rv_pcmixer::stream_start()
{
    std::lock_guard<std::mutex> guard(lock_);
    stream_.start();
}

int64_t rv_pcmixer::stream_push(const int16_t *src, int64_t frames)
{
    std::lock_guard<std::mutex> guard(lock_);
    return stream_.push(src, frames);
}

void rv_pcmixer::stream_end_input()
{
    std::lock_guard<std::mutex> guard(lock_);
    stream_.end_input();
}

void rv_pcmixer::stream_clear()
{
    std::lock_guard<std::mutex> guard(lock_);
    stream_.clear();
}

rv_pcstream_report rv_pcmixer::stream_report() const
{
    std::lock_guard<std::mutex> guard(lock_);
    return stream_.report();
}

// THEOREM: saturating summation. Voices and the stream input are summed in int32
// and CLIPPED into int16 at the very end - they are never divided by the number of voices.
//
// Averaging looks safer and is worse: it makes the gain of a voice depend on how
// many other voices happen to be sounding, so the same footstep is loud alone
// and thin in a crowd, and a voice retiring makes everything else jump. The
// disc would be mixing against a moving target it cannot observe. With plain
// addition, ONE voice at unity is exactly its recorded level, always; the
// arithmetic is exact until the sum leaves int16 range, and beyond that it
// clips — which is what the hardware does and what a sound designer already
// knows how to budget for. int32 has ~65000x headroom over a single full-scale
// voice, so 24 voices cannot overflow the accumulator itself; only the final
// narrowing can lose anything, and it loses it audibly and predictably.
void rv_pcmixer::render(int16_t *out, int64_t frames)
{
    if (!out || frames <= 0) {
        return;
    }

    std::lock_guard<std::mutex> guard(lock_);

    int64_t done = 0;
    while (done < frames) {
        const int64_t block = std::min(RV_PCMIXER_BLOCK_FRAMES, frames - done);
        const std::size_t values = static_cast<std::size_t>(block * RV_PCMIXER_CHANNELS);

        std::fill(accumulator_.begin(), accumulator_.begin() + static_cast<std::ptrdiff_t>(values), 0);
        for (rv_pcvoice &voice : voices_) {
            voice.mix(accumulator_.data(), block);
        }
        stream_.mix(accumulator_.data(), block);

        int16_t *dst = out + done * RV_PCMIXER_CHANNELS;
        for (std::size_t i = 0; i < values; ++i) {
            // Mute is the LAST thing that happens to a sample: everything above
            // ran exactly as it would have unmuted.
            dst[i] = muted_ ? static_cast<int16_t>(0) : saturate(accumulator_[i]);
        }

        done += block;
    }
}

} // namespace rv_3dmppc
