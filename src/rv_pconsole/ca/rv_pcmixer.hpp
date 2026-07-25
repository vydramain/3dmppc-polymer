// ─── NEUROSLOP ────────────────────────────────────────────────────────────────
// Сгенерировано Claude (claude-opus-5). Не проверено человеком.
// Ревизия: stage 7 — микшер: 24 голоса в стерео S16 и единственный мьютекс SPU.
// ──────────────────────────────────────────────────────────────────────────────
//
// The console's mixing stage: it owns the voices and turns them into the stereo
// stream the host hands to the device.
//
// This is also THE MEETING POINT OF THE TWO THREADS of the machine. The game
// thread arrives through rv_ca (voice_setup / voice_play / voice_stop /
// voice_status); the device thread arrives through render(), called from SDL's
// audio callback. Nothing else in the console is touched from two threads.
//
// DECISION (do not re-litigate): the meeting is guarded by ONE MUTEX, not by a
// lock-free command queue. The critical section is a few dozen microseconds of
// arithmetic over 24 fixed-size voice structs — it does not allocate, it does
// not block, it cannot recurse, and it holds no lock while calling anything
// else. A lock-free queue would buy nothing measurable here and would cost the
// one thing this project cannot spare: the ability to read the audio path and
// believe it. If priority inversion ever shows up in a profile, the fix is a
// smaller critical section, not a redesign.
//
// Knows nothing about SDL: it fills an int16 buffer and never learns where it
// goes. SDL is confined to rv_pchost.cpp by house rule.
#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "pdk/ca/rv_voice_conf.hpp"
#include "rv_pconsole/ca/rv_pcvoice.hpp"

namespace rv_3dmppc {

// Frames the mixer sums in one pass. render() chops any request into blocks of
// at most this size so its int32 accumulator can be allocated ONCE, at
// construction: the device thread must never wait on the heap.
constexpr int64_t RV_PCMIXER_BLOCK_FRAMES = 512;

// Channels on the output side. The voices are mono (see rv_pcvoice.hpp).
constexpr int64_t RV_PCMIXER_CHANNELS = 2;

class rv_pcmixer {
   public:
    explicit rv_pcmixer(int64_t voice_count);

    rv_pcmixer(const rv_pcmixer&) = delete;
    rv_pcmixer& operator=(const rv_pcmixer&) = delete;

    int64_t voice_count() const { return static_cast<int64_t>(voices_.size()); }

    // Silence the OUTPUT STAGE only: the voices keep running, keep consuming
    // their samples and keep reporting themselves busy, and only the samples
    // leaving render() are zeroed. A muted console must stay the same machine
    // from the disc's point of view (see rv_pcca_conf::mute).
    void set_muted(bool muted);

    // --- the game thread's side; each call takes the lock for its duration ---

    // Load `conf` into every voice named by `mask`. `data` / `frames` describe
    // the sound-RAM region conf.sample_address resolves to.
    void setup(int64_t mask, const rv_voice_conf& conf, const uint8_t* data, int64_t frames,
               int64_t addr);

    // Start / stop every voice in `mask`. Returns false when some voice in the
    // mask was never armed — in which case NOTHING is started or stopped, so a
    // malformed call cannot leave half the mask sounding.
    bool play(int64_t mask);
    bool stop(int64_t mask);

    // Is every voice in `mask` armed? The same question play() asks before it
    // acts, for the caller that must validate a call it is not going to run
    // (a console whose audio device never opened — see rv_pcca.cpp).
    bool armed(int64_t mask) const;

    // Mask of the voices in `mask` that are still busy.
    int64_t status(int64_t mask) const;

    // --- the device thread's side ---

    // Sum every sounding voice into `out`, which holds `frames` INTERLEAVED
    // stereo frames (2 * frames int16 values). Overwrites; does not accumulate.
    void render(int16_t* out, int64_t frames);

    // --- compound operations, for the caller that must be atomic ---

    // Take the lock by hand. rv_pcca needs it around work the mixer knows
    // nothing about — writing bytes into a region a voice may be reading this
    // instant, and freeing one after checking that nobody is.
    //
    // RULE: never call another rv_pcmixer method while holding this. The mutex
    // is not recursive and the *_locked helpers below exist precisely so that
    // the compound operations need not try.
    [[nodiscard]] std::unique_lock<std::mutex> acquire();

    // Is any voice sounding out of the region at `addr`? This is the question
    // behind rv_ca::sound_asset_free returning RV_ERR_BUSY. Caller holds
    // acquire().
    bool region_busy_locked(int64_t addr) const;

    // Disarm every voice pointing at `addr`, called once that region has been
    // released. The voices are provably idle by then (region_busy_locked said
    // so), and leaving them armed would let a later voice_play() read bytes the
    // pool has already handed to somebody else. Caller holds acquire().
    void disarm_region_locked(int64_t addr);

   private:
    bool for_each_locked(int64_t mask, bool require_armed, void (rv_pcvoice::*action)());

    mutable std::mutex lock_;
    std::vector<rv_pcvoice> voices_;

    // Summing accumulator, sized once and reused. Lives here rather than on the
    // device thread's stack so its size is a property of the mixer.
    std::vector<int32_t> accumulator_;

    bool muted_ = false;
};

}  // namespace rv_3dmppc
