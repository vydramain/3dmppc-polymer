// Combat resolution — Agent C. The single place damage is decided (runs LAST
// among gameplay systems). Integrates brick projectiles and resolves brick and
// pipe hits on enemies; a player hit this frame breaks the ritual.
#include "game/combat.hpp"

#include <cmath>

#include "game/context.hpp"
#include "game/game_state.hpp"
#include "game/ritual.hpp"

namespace rv_3dmppc {

namespace {

constexpr float kGravity   = 14.0f;  // m/s^2 on thrown bricks
constexpr float kBrickHitR = 0.6f;   // brick/enemy overlap radius (XZ)
constexpr float kBrickTop  = 1.6f;   // enemy body height for the brick sphere
constexpr float kBrickDmg  = 6.0f;
constexpr float kPipeDmg   = 5.0f;
constexpr float kPipeRange = 1.6f;   // pipe reach in front of the player
constexpr float kPipeArc   = 0.3f;   // dot(forward, toEnemy) threshold (~72 deg cone)
constexpr float kHitstop   = 0.07f;  // punchy micro-freeze on a solid hit
constexpr float kHitFlash  = 0.15f;  // matches enemies.cpp draw normalization

float xzLen(const Vec3& a) { return std::sqrt(a.x * a.x + a.z * a.z); }

// Punch: nudge the player's hitstop up (owned by Agent B; combat drives feel).
void punch(GameState& gs) {
    if (gs.player.hitstop < kHitstop) gs.player.hitstop = kHitstop;
}

}  // namespace

void updateCombat(GameState& gs, const GameContext& ctx, float dt) {
    // --- 1. Integrate + resolve brick projectiles --------------------------
    for (Projectile& p : gs.projectiles) {
        if (!p.active) continue;

        p.vel.y -= kGravity * dt;
        p.pos = p.pos + p.vel * dt;
        p.life -= dt;

        // Despawn on lifetime end or ground contact.
        if (p.life <= 0.0f || p.pos.y <= 0.0f) { p.active = false; continue; }

        // Despawn on world collider (wall / big prop).
        bool blocked = false;
        for (const AABB& c : gs.world.colliders) {
            if (c.containsXZ(p.pos) && p.pos.y >= c.min.y && p.pos.y <= c.max.y) {
                blocked = true;
                break;
            }
        }
        if (blocked) { p.active = false; continue; }

        if (p.hasHit) continue;

        // Enemy hit test: XZ sphere overlap within the enemy's body height.
        for (int i = 0; i < static_cast<int>(gs.enemies.list.size()); ++i) {
            Enemy& e = gs.enemies.list[i];
            if (!e.alive) continue;
            const Vec3 dXZ{p.pos.x - e.pos.x, 0.0f, p.pos.z - e.pos.z};
            if (xzLen(dXZ) <= kBrickHitR && p.pos.y <= kBrickTop) {
                damageEnemy(gs, ctx, i, kBrickDmg);
                p.hasHit = true;
                p.active = false;
                ctx.audio->playSfx(Audio::Sfx::BrickImpact);
                punch(gs);
                break;  // one brick, one enemy
            }
        }
    }

    // --- 2. Pipe melee — arc/range test on the swing's rising edge ---------
    // Only fire on the frame pipeHitActive first becomes true, so one swing
    // deals damage once per enemy instead of every active frame.
    if (gs.weapons.pipeHitActive && !gs.enemies.pipePrevActive) {
        const Vec3 pp = gs.player.pos;
        const Vec3 fwd{std::sin(gs.player.yaw), 0.0f, std::cos(gs.player.yaw)};
        bool connected = false;
        for (int i = 0; i < static_cast<int>(gs.enemies.list.size()); ++i) {
            Enemy& e = gs.enemies.list[i];
            if (!e.alive) continue;
            const Vec3 toXZ{e.pos.x - pp.x, 0.0f, e.pos.z - pp.z};
            const float dist = xzLen(toXZ);
            if (dist > kPipeRange) continue;
            if (dot(normalize(toXZ), fwd) < kPipeArc) continue;  // outside the cone
            damageEnemy(gs, ctx, i, kPipeDmg);
            connected = true;
        }
        if (connected) {
            ctx.audio->playSfx(Audio::Sfx::PipeImpact);
            punch(gs);
        }
    }
    gs.enemies.pipePrevActive = gs.weapons.pipeHitActive;  // latch for next frame

    // --- 3. A player hit this frame breaks the active ritual step ----------
    // enemies.cpp (and any player-damage path here) set playerHitThisFrame when
    // damagePlayer is triggered; the frame flag is cleared in solid.cpp.
    if (gs.playerHitThisFrame) interruptRitual(gs);
}

void damageEnemy(GameState& gs, const GameContext& ctx, int index, float amount) {
    if (index < 0 || index >= static_cast<int>(gs.enemies.list.size())) return;
    Enemy& e = gs.enemies.list[index];
    if (!e.alive) return;
    e.hp -= amount;
    e.hitFlash = kHitFlash;  // brief white flash (drawn in enemies.cpp)
    ctx.audio->playSfx(Audio::Sfx::EnemyHurt);
    if (e.hp <= 0.0f) e.alive = false;
}

}  // namespace rv_3dmppc
