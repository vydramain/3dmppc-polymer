// Per-frame services handed to every game system: audio, save, and a
// deterministic RNG. Passed by const-ref (rng is mutable through the pointer)
// so systems stay free functions over GameState + GameContext.
//
// SHARED CONTRACT — owned by the orchestrator.
//
// Note: the workflow/runtime forbids wall-clock/random seeding at some layers,
// and determinism makes the template reproducible for tests — so the RNG is a
// tiny explicit-seed xorshift, never seeded from the clock.
#pragma once

#include <cstdint>

#include "core/audio.hpp"
#include "core/savecard.hpp"

namespace rv_3dmppc {

// Minimal deterministic RNG (xorshift64*). Seed is fixed unless the disc reseeds
// it (e.g. from the day count) so runs are reproducible.
struct Rng {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;

    std::uint64_t nextU64() {
        std::uint64_t x = state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state = x;
        return x * 0x2545F4914F6CDD1Dull;
    }
    // Uniform float in [0,1).
    float unit() { return (nextU64() >> 40) * (1.0f / 16777216.0f); }
    // Uniform float in [lo,hi).
    float range(float lo, float hi) { return lo + unit() * (hi - lo); }
    // Uniform int in [lo,hi].
    int rangeI(int lo, int hi) {
        return lo + static_cast<int>(nextU64() % static_cast<std::uint64_t>(hi - lo + 1));
    }
    void reseed(std::uint64_t s) { state = s ? s : 0x9E3779B97F4A7C15ull; }
};

struct GameContext {
    Audio* audio = nullptr;   // never null in practice; NullAudio if no device
    SaveCard* save = nullptr; // never null; NullSaveCard in headless
    mutable Rng rng;
};

}  // namespace rv_3dmppc
