// Combat resolution — the single place damage is decided. OWNED BY AGENT C.
//
// Integrates brick projectiles (arc + gravity + lifetime) and resolves:
//   * projectile → enemy hits (brick),
//   * pipe melee → enemy hits (reads WeaponState::pipeHitActive + player transform),
//   * enemy attacks → player (on telegraph resolve),
//   * deaths (enemy hp<=0 → alive=false), hitstop/shake via damage helpers.
//
// Feedback timings (mechanics.md): hitstop ~0.06–0.1s, micro-shake, loud SFX.
#pragma once

namespace rv_3dmppc {

struct GameState;
struct GameContext;

// Resolve all damage for this frame. Call after weapons + enemies updates.
void updateCombat(GameState& gs, const GameContext& ctx, float dt);

// Deal damage to an enemy (index into EnemyWorld::list); handles flash + death.
void damageEnemy(GameState& gs, const GameContext& ctx, int index, float amount);

}  // namespace rv_3dmppc
