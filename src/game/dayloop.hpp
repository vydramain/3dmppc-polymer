// The day loop — the game's spine: Home → Street → Factory → Return, with a
// summary, one Home mutation per successful loop, and persistence to the memory
// card. Win = complete the ritual and return home once; Lose = knocked out
// anywhere → restart the day. OWNED BY AGENT D.
#pragma once

#include <cstdint>

#include "core/input.hpp"
#include "game/areas.hpp"

namespace rv_3dmppc {

struct GameState;
struct GameContext;

enum class Phase {
    Home,      // preparation
    Street,    // commute + fight
    Factory,   // ritual + escalation
    Return,    // day summary; Home mutates
    GameOver,  // knocked out → restart prompt
};

// Small POD persisted to the memory card. Keep it trivially copyable.
struct SaveBlob {
    std::uint32_t magic = 0x534F4C44;  // 'SOLD'
    std::uint32_t version = 1;
    std::int32_t day = 1;
    std::int32_t corruption = 0;
    std::int32_t homeMutations = 0;
    std::int32_t loopsCompleted = 0;
};

struct DayLoopState {
    // ---- stable (read by hud, areas, spawner escalation) ------------------
    int day = 1;
    int corruption = 0;      // world mutation / mood level
    int homeMutations = 0;   // visible Home progression
    int loopsCompleted = 0;
    Phase phase = Phase::Home;

    // ---- transient -------------------------------------------------------
    bool showSummary = false;
    float summaryTimer = 0.0f;
    bool started = false;
};

// Load save (if any), init the first area. Called from Disc::boot.
void bootDayLoop(GameState& gs, const GameContext& ctx);

// Drive phase transitions: exits, ritual completion, death, summary, mutation,
// and save writes. Loads areas via loadArea and toggles encounters.
void updateDayLoop(GameState& gs, const GameContext& ctx, const InputState& in, float dt);

}  // namespace rv_3dmppc
