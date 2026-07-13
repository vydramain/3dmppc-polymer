// The Factory lamppost ritual (mechanics.md): a 3-step hold-to-assemble under
// pressure. Taking a hit interrupts the current step; the encounter escalates
// once around step 2; on completion the return trigger home is armed.
// OWNED BY AGENT D.
#pragma once

#include "core/input.hpp"
#include "types.hpp"
#include "math/math.hpp"

namespace rv_3dmppc {

struct GameState;
struct GameContext;

struct RitualState {
    bool available = false;   // player is near the socket and can assemble
    bool active = false;      // currently holding a step
    int step = 0;             // completed steps [0..3]
    float progress = 0.0f;    // current step hold progress [0,1]
    bool complete = false;    // all 3 steps done → return armed
    bool escalated = false;   // escalation fired once (around step 2)
    Vec3 socketPos{0, 0, 0};

    static constexpr int kSteps = 3;
    static constexpr float kStepSeconds = 2.5f;  // hold time per step
};

// Reset ritual for a fresh Factory visit, anchored at the area's socket.
void initRitual(GameState& gs, const Vec3& socketPos);

// Advance the hold if near the socket + interact held; interrupt on damage.
void updateRitual(GameState& gs, const GameContext& ctx, const InputState& in, float dt);

// Socket + progress + finished-lamppost visuals.
void collectRitualDraws(const GameState& gs, DrawList& out);

// Combat calls this when the player is hit, to interrupt the active step.
void interruptRitual(GameState& gs);

}  // namespace rv_3dmppc
