// Enemy bestiary + per-enemy AI. OWNED BY AGENT C.
//
// Two archetypes ship in the slice (characters.md / mechanics.md):
//   * Kipuchka — fast jittery melee (no-steal stub first).
//   * Midnight Smoker — expanding smoke-cloud AoE with a pre-warm ring.
//
// Strong telegraphs: windups ≥ ~300 ms. Damage to the player is applied via
// combat (updateCombat) or here through damagePlayer(); deaths flip alive=false.
#pragma once

#include <vector>

#include "types.hpp"
#include "math/math.hpp"

namespace rv_3dmppc {

struct GameState;
struct GameContext;

enum class EnemyType { Kipuchka, Smoker };

struct Enemy {
    // ---- stable (read by combat, spawner, ritual escalation, hud) ---------
    EnemyType type = EnemyType::Kipuchka;
    Vec3 pos{0, 0, 0};
    float yaw = 0.0f;
    float hp = 10.0f;
    bool alive = true;

    // ---- AI / telegraph state (Agent C owns) ------------------------------
    int state = 0;              // per-type state machine index
    float stateTimer = 0.0f;
    float telegraph = 0.0f;     // >0 during an attack windup (≥0.3s)
    float attackCooldown = 0.0f;
    float hitFlash = 0.0f;      // brief tint on taking damage
    // Smoker AoE:
    bool aoeWarn = false;       // pre-warm ring visible
    float aoeRadius = 0.0f;     // current cloud radius (0 = no cloud)
};

struct EnemyWorld {
    std::vector<Enemy> list;
    int alive = 0;  // cached count of living enemies (maintained by updateEnemies)

    // Rising-edge latch for the pipe swing so a single swing hits each enemy
    // once instead of every frame WeaponState::pipeHitActive stays true.
    // Owned + updated by combat (updateCombat). Agent C.
    bool pipePrevActive = false;
};

void updateEnemies(GameState& gs, const GameContext& ctx, float dt);
void collectEnemyDraws(const GameState& gs, DrawList& out);

}  // namespace rv_3dmppc
