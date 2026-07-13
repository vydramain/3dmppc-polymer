// First-person player: movement, look, health, and the camera feel (head-bob,
// micro-shake, hitstop). OWNED BY AGENT B.
//
// Other systems read the "stable" fields below (pos/yaw/pitch/hp/alive/radius).
// Agent B may ADD fields freely but must not rename/remove the stable ones.
#pragma once

#include <vector>

#include "core/input.hpp"
#include "gpu/camera.hpp"
#include "types.hpp"
#include "math/math.hpp"

namespace rv_3dmppc {

struct GameState;
struct GameContext;

struct PlayerState {
    // ---- stable (read by enemies, combat, ritual, world, hud) -------------
    Vec3 pos{0.0f, 1.6f, 0.0f};  // eye position (y ≈ eye height)
    float yaw = 0.0f;            // radians around +y
    float pitch = 0.0f;          // radians, clamped
    float hp = 100.0f;
    float maxHp = 100.0f;
    bool alive = true;
    float radius = 0.3f;         // horizontal collision radius

    // ---- feel / internal (Agent B owns; extend as needed) -----------------
    float hitstop = 0.0f;        // >0 freezes the sim briefly on a solid hit
    Vec3 shake{0, 0, 0};         // additive camera shake offset
    float shakeMag = 0.0f;
    float bobPhase = 0.0f;
    Vec3 velocity{0, 0, 0};
    float distSinceStep = 0.0f;   // metres walked since the last footstep SFX
};

// Advance movement/look/feel. Movement resolves against state.world.colliders.
// When state.player.hitstop > 0, honor it (freeze motion) then decay it.
void updatePlayer(GameState& gs, const GameContext& ctx, const InputState& in, float dt);

// Build the render camera from the player transform + bob/shake offsets.
Camera playerCamera(const GameState& gs);

// First-person held item / hands go through the weapons system; nothing here.

// Combat calls this to apply damage + trigger hitstop/shake. (Impl in player.cpp
// so the feel lives with the player; combat only decides *when*.)
void damagePlayer(GameState& gs, const GameContext& ctx, float amount);

}  // namespace rv_3dmppc
