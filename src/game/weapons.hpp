// Improvised weapons: brick (throwable primary) and pipe (melee backup).
// OWNED BY AGENT B.
//
// Weapons spawn brick projectiles into GameState::projectiles and drive the pipe
// swing. Actual damage resolution (projectile→enemy, pipe→enemy) lives in the
// combat system (Agent C), which reads the stable fields marked below.
#pragma once

#include "core/input.hpp"
#include "game/types.hpp"
#include "math/math.hpp"

namespace rv_3dmppc {

struct GameState;
struct GameContext;

struct WeaponState {
    enum Kind { Brick = 0, Pipe = 1 };

    // ---- stable (read by combat/hud) --------------------------------------
    int active = Brick;
    int brickAmmo = 5;
    float brickCharge = 0.0f;   // [0,1] while the throw is held/readied
    float brickCooldown = 0.0f; // seconds until next throw allowed
    float pipeCooldown = 0.0f;  // seconds until next swing allowed
    float pipeSwing = 0.0f;     // >0 while a swing animation plays
    bool pipeHitActive = false; // true on the frame(s) the swing can connect

    // ---- internal (Agent B owns) ------------------------------------------
    bool brickHeld = false;
};

// Ready/throw brick, swing pipe, swap weapon (context button). Spawns projectiles.
void updateWeapons(GameState& gs, const GameContext& ctx, const InputState& in, float dt);

// First-person held item (ViewBrick/ViewPipe/ViewHand) into the draw list.
void collectWeaponDraws(const GameState& gs, DrawList& out);

// Give the player a brick from a world pickup, capped sensibly.
void addBrickAmmo(GameState& gs, int n);

}  // namespace rv_3dmppc
