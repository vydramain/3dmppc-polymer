// Enemy spawner discipline (mechanics.md): cap ~5 active, enforce a minimum
// spawn distance from the player, and a ~1.5s spawn grace. Street spawns are
// chunk-indexed; Factory spawns escalate the ritual encounter. OWNED BY AGENT C.
#pragma once

#include "enemies.hpp"
#include "types.hpp"
#include "math/math.hpp"

namespace rv_3dmppc {

struct GameState;
struct GameContext;

struct SpawnConfig {
    int cap = 5;
    float minDist = 6.0f;   // never spawn closer than this to the player
    float grace = 1.5f;     // min seconds between spawns
    float interval = 3.0f;  // target spacing of spawn attempts
};

struct SpawnerState {
    bool enabled = false;      // world/dayloop toggles this per area
    float sinceLast = 0.0f;
    int budget = 0;            // remaining enemies this encounter (-1 = endless)
    SpawnConfig cfg{};
    // Candidate spawn points supplied by the current area.
    std::vector<Vec3> points;
};

// Toggle + configure the spawner when entering/leaving an encounter.
void beginEncounter(GameState& gs, int budget, const SpawnConfig& cfg);
void endEncounter(GameState& gs);

// Attempt spawns respecting cap/min-distance/grace.
void updateSpawner(GameState& gs, const GameContext& ctx, float dt);

// Direct spawn helper (used by scripted placement).
void spawnEnemyAt(GameState& gs, EnemyType type, const Vec3& pos);

}  // namespace rv_3dmppc
