// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 7 — сведение голосов с насыщением и критическая секция SPU.
// ──────────────────────────────────────────────────────────────────────────────
#include "rv_pconsole/ca/rv_pcmixer.hpp"

#include <algorithm>

namespace rv_3dmppc {
namespace {

constexpr int32_t RV_PCMIXER_S16_MAX = 32767;
constexpr int32_t RV_PCMIXER_S16_MIN = -32768;

int16_t saturate(int32_t value) {
    if (value > RV_PCMIXER_S16_MAX) return static_cast<int16_t>(RV_PCMIXER_S16_MAX);
    if (value < RV_PCMIXER_S16_MIN) return static_cast<int16_t>(RV_PCMIXER_S16_MIN);
    return static_cast<int16_t>(value);
}

bool mask_has(int64_t mask, int64_t index) {
    return (mask & (static_cast<int64_t>(1) << index)) != 0;
}

}  // namespace

rv_pcmixer::rv_pcmixer(int64_t voice_count)
    : voices_(static_cast<std::size_t>(voice_count > 0 ? voice_count : 0)),
      accumulator_(static_cast<std::size_t>(RV_PCMIXER_BLOCK_FRAMES * RV_PCMIXER_CHANNELS), 0) {}

void rv_pcmixer::set_muted(bool muted) {
    std::lock_guard<std::mutex> guard(lock_);
    muted_ = muted;
}

void rv_pcmixer::setup(int64_t mask, const rv_voice_conf& conf, const uint8_t* data, int64_t frames,
                       int64_t addr) {
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
bool rv_pcmixer::for_each_locked(int64_t mask, bool require_armed, void (rv_pcvoice::*action)()) {
    if (require_armed) {
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            if (mask_has(mask, static_cast<int64_t>(i)) && !voices_[i].armed()) return false;
        }
    }

    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (mask_has(mask, static_cast<int64_t>(i))) (voices_[i].*action)();
    }
    return true;
}

bool rv_pcmixer::play(int64_t mask) {
    std::lock_guard<std::mutex> guard(lock_);
    return for_each_locked(mask, true, &rv_pcvoice::play);
}

bool rv_pcmixer::stop(int64_t mask) {
    std::lock_guard<std::mutex> guard(lock_);
    return for_each_locked(mask, true, &rv_pcvoice::stop);
}

bool rv_pcmixer::armed(int64_t mask) const {
    std::lock_guard<std::mutex> guard(lock_);
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (mask_has(mask, static_cast<int64_t>(i)) && !voices_[i].armed()) return false;
    }
    return true;
}

int64_t rv_pcmixer::status(int64_t mask) const {
    std::lock_guard<std::mutex> guard(lock_);

    int64_t busy = 0;
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        const int64_t bit = static_cast<int64_t>(i);
        if (mask_has(mask, bit) && voices_[i].busy()) busy |= static_cast<int64_t>(1) << bit;
    }
    return busy;
}

std::unique_lock<std::mutex> rv_pcmixer::acquire() { return std::unique_lock<std::mutex>(lock_); }

bool rv_pcmixer::region_busy_locked(int64_t addr) const {
    for (const rv_pcvoice& voice : voices_) {
        if (voice.busy() && voice.region() == addr) return true;
    }
    return false;
}

void rv_pcmixer::disarm_region_locked(int64_t addr) {
    for (rv_pcvoice& voice : voices_) {
        if (voice.region() == addr) voice.disarm();
    }
}

// THEOREM: saturating summation. Voices are summed in int32 and CLIPPED into
// int16 at the very end — they are never divided by the number of voices.
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
void rv_pcmixer::render(int16_t* out, int64_t frames) {
    if (!out || frames <= 0) return;

    std::lock_guard<std::mutex> guard(lock_);

    int64_t done = 0;
    while (done < frames) {
        const int64_t block = std::min(RV_PCMIXER_BLOCK_FRAMES, frames - done);
        const std::size_t values = static_cast<std::size_t>(block * RV_PCMIXER_CHANNELS);

        std::fill(accumulator_.begin(), accumulator_.begin() + static_cast<std::ptrdiff_t>(values),
                  0);
        for (rv_pcvoice& voice : voices_) voice.mix(accumulator_.data(), block);

        int16_t* dst = out + done * RV_PCMIXER_CHANNELS;
        for (std::size_t i = 0; i < values; ++i) {
            // Mute is the LAST thing that happens to a sample: everything above
            // ran exactly as it would have unmuted.
            dst[i] = muted_ ? static_cast<int16_t>(0) : saturate(accumulator_[i]);
        }

        done += block;
    }
}

}  // namespace rv_3dmppc
