// Console memory-card service — a small persistent blob store, ~128 KB (see
// docs/platform/specs.md). The disc's loop flags fit easily.
//
// SHARED INTERFACE — owned by the orchestrator. Concrete file-backed
// implementation is Agent A's (core/savecard.cpp). Game code (Agent D's day
// loop) reads/writes a small POD blob through GameContext::save.
#pragma once

#include <cstddef>

namespace rv_3dmppc {

class SaveCard {
public:
    static constexpr std::size_t kCapacity = 128 * 1024;

    virtual ~SaveCard() = default;

    // Read up to `n` bytes of the saved blob into `dst`. Returns false if there
    // is no valid save yet (caller should treat as "fresh card").
    virtual bool read(void* dst, std::size_t n) = 0;

    // Persist `n` bytes (n <= kCapacity). Returns false on I/O failure.
    virtual bool write(const void* src, std::size_t n) = 0;
};

// In-memory card that never persists — used for headless smoke tests and as a
// safe fallback. Agent A provides the file-backed one.
class NullSaveCard final : public SaveCard {
public:
    bool read(void*, std::size_t) override { return false; }
    bool write(const void*, std::size_t) override { return true; }
};

}  // namespace rv_3dmppc
