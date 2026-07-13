// The three areas — Home, Street, Factory — as greybox geometry + colliders +
// triggers, and the area manager that (re)builds them. OWNED BY AGENT D.
//
// An area owns: static draw geometry, player colliders, the player spawn, and an
// exit trigger that advances the day loop. Home also owns the mutation slots
// that accumulate across successful loops.
#pragma once

#include <vector>

#include "game/types.hpp"
#include "math/math.hpp"

namespace rv_3dmppc {

struct GameState;
struct GameContext;

enum class Area { Home, Street, Factory };

struct AreaWorld {
    Area current = Area::Home;

    std::vector<AABB> colliders;   // player collision (walls, big props)
    DrawList staticDraws;          // pre-built geometry for the current area

    Vec3 playerSpawn{0, 1.6f, 0};
    float playerSpawnYaw = 0.0f;

    AABB exitTrigger{};            // walk in to advance (gate / stairwell / return)
    bool exitArmed = false;        // some exits unlock only after an objective

    // Factory ritual socket position (fed to RitualState on load).
    Vec3 socketPos{0, 0, 0};
    bool hasSocket = false;

    // Street chunk-indexed spawn points / Factory spawn points (fed to spawner).
    std::vector<Vec3> spawnPoints;

    // ---- Agent D additive: pickups + exit latch --------------------------
    // A world Brick pickup: a walk-in trigger that grants ammo. Its marker
    // (MeshId::Pickup) is drawn at `pos` while active; taken pickups go inactive.
    struct Pickup {
        AABB trigger{};
        Vec3 pos{0, 0, 0};
        int ammo = 1;
        bool active = true;
    };
    std::vector<Pickup> pickups;

    // Set by updateAreas when the player stands in the *armed* exit trigger;
    // playerAtExit() returns this and the day loop reads it to advance phase.
    bool atExit = false;
};

// Rebuild `gs.world` for the given area (geometry, colliders, spawn, triggers).
// `homeMutations` controls how many mutation props Home shows.
void loadArea(GameState& gs, const GameContext& ctx, Area area);

// Per-frame area logic: exit-trigger detection sets a flag the day loop reads.
void updateAreas(GameState& gs, const GameContext& ctx, float dt);

// True once the player has entered the (armed) exit trigger this area.
bool playerAtExit(const GameState& gs);

void collectAreaDraws(const GameState& gs, DrawList& out);

}  // namespace rv_3dmppc
