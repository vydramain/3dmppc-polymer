// Enemy AI + draws — Agent C. Two archetypes: Kipuchka (fast jittery melee) and
// Midnight Smoker (area-denial smoke cloud). Damage to the player is applied
// here through damagePlayer(); combat (updateCombat) resolves everything else.
//
// Determinism: only ctx.rng is used for jitter/variety, never the clock/rand.
#include "game/enemies.hpp"

#include <cmath>

#include "game/context.hpp"
#include "game/game_state.hpp"

namespace rv_3dmppc {

namespace {

// ---- shared tuning ---------------------------------------------------------
constexpr float kHitFlash = 0.15f;   // seconds a hurt enemy flashes white

// ---- Kipuchka --------------------------------------------------------------
constexpr float kKipSpeed    = 3.4f;   // m/s chase speed (fast pest)
constexpr float kKipReach    = 1.2f;   // melee engage range
constexpr float kKipWindup   = 0.35f;  // telegraph length (>= 0.30s rule)
constexpr float kKipDamage   = 8.0f;   // hit damage to player
constexpr float kKipCooldown = 1.2f;   // recovery between swings
constexpr float kKipJitter   = 0.35f;  // lateral twitch amount [-j..j]
constexpr float kKipScale    = 0.75f;  // small silhouette

// ---- Midnight Smoker -------------------------------------------------------
constexpr float kSmokeSpeed   = 1.4f;  // m/s slow approach
constexpr float kSmokeStand   = 5.0f;  // preferred stand-off distance
constexpr float kSmokeGap     = 3.0f;  // seconds between AoE bursts
constexpr float kSmokeWarn    = 0.5f;  // pre-warm ring duration
constexpr float kSmokeGrow    = 1.0f;  // cloud grow time
constexpr float kSmokeHold    = 0.8f;  // cloud full-size hold time
constexpr float kSmokeShrink  = 1.0f;  // cloud shrink time
constexpr float kSmokeMaxR    = 3.0f;  // max cloud radius
constexpr float kSmokeChip    = 3.0f;  // chip damage per tick inside the cloud
constexpr float kSmokeChipGap = 0.5f;  // seconds between chip ticks
constexpr float kSmokeScaleXZ = 0.9f;
constexpr float kSmokeScaleY  = 1.25f;

// Smoker per-type state indices (Enemy::state).
enum { SmokeApproach = 0, SmokeWarn = 1, SmokeCloudPhase = 2 };
// Kipuchka per-type state indices.
enum { KipChase = 0, KipWindup = 1 };

float xzLen(const Vec3& a) { return std::sqrt(a.x * a.x + a.z * a.z); }

// Component-wise lerp for tint blending.
Vec3 lerp3(const Vec3& a, const Vec3& b, float t) {
    return {lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t)};
}

void updateKipuchka(GameState& gs, const GameContext& ctx, Enemy& e, float dt) {
    const Vec3 toXZ{gs.player.pos.x - e.pos.x, 0.0f, gs.player.pos.z - e.pos.z};
    const float dist = xzLen(toXZ);
    const Vec3 dir = normalize(toXZ);

    if (e.state == KipChase) {
        // Twitchy: add a perpendicular jitter each frame so it never tracks in
        // a straight line. rng only, so replays stay deterministic.
        const float j = ctx.rng.range(-kKipJitter, kKipJitter);
        const Vec3 perp{-dir.z, 0.0f, dir.x};
        const Vec3 move = normalize(dir + perp * j);
        if (dist > kKipReach) {
            e.pos = e.pos + move * (kKipSpeed * dt);
            e.yaw = std::atan2(move.x, move.z);  // face movement dir
        } else {
            e.yaw = std::atan2(dir.x, dir.z);    // face player while poised
        }
        if (dist <= kKipReach && e.attackCooldown <= 0.0f) {
            e.state = KipWindup;
            e.telegraph = kKipWindup;  // begin readable windup
        }
    } else {  // KipWindup — committed to a strike, hold still and face player
        e.yaw = std::atan2(dir.x, dir.z);
        e.telegraph -= dt;
        if (e.telegraph <= 0.0f) {
            // Land the hit only if the player is still in reach (small leniency
            // so a grazing dodge is possible but tight).
            if (dist <= kKipReach * 1.2f) {
                damagePlayer(gs, ctx, kKipDamage);
                gs.playerHitThisFrame = true;  // signal ritual interrupt
                e.attackCooldown = kKipCooldown;
            }
            // NO-STEAL STUB: on repeated hits a Kipuchka would stun the player,
            // steal the pillar/lamppost component and flee into an alley (see
            // mechanics.md). Recovery = chase it down. Not shipped in the slice.
            e.telegraph = 0.0f;
            e.state = KipChase;
        }
    }
}

void updateSmoker(GameState& gs, const GameContext& ctx, Enemy& e, float dt) {
    const Vec3 toXZ{gs.player.pos.x - e.pos.x, 0.0f, gs.player.pos.z - e.pos.z};
    const float dist = xzLen(toXZ);
    const Vec3 dir = normalize(toXZ);
    e.yaw = std::atan2(dir.x, dir.z);  // always face the player

    if (e.state == SmokeApproach) {
        if (dist > kSmokeStand) e.pos = e.pos + dir * (kSmokeSpeed * dt);
        e.stateTimer += dt;
        // Once settled at range, periodically arm the next cloud.
        if (dist <= kSmokeStand + 1.0f && e.stateTimer >= kSmokeGap) {
            e.state = SmokeWarn;
            e.stateTimer = 0.0f;
            e.aoeWarn = true;
            ctx.audio->playSfx(Audio::Sfx::AoeWarn);  // one cue at warn start
        }
    } else if (e.state == SmokeWarn) {
        e.aoeWarn = true;  // pre-warm ring visible (readable >= 0.3s window)
        e.stateTimer += dt;
        if (e.stateTimer >= kSmokeWarn) {
            e.state = SmokeCloudPhase;
            e.stateTimer = 0.0f;
            e.aoeWarn = false;
            e.attackCooldown = 0.0f;  // repurpose as chip-tick timer below
        }
    } else {  // SmokeCloudPhase — grow, hold, shrink
        e.stateTimer += dt;
        const float t = e.stateTimer;
        if (t < kSmokeGrow) {
            e.aoeRadius = kSmokeMaxR * (t / kSmokeGrow);
        } else if (t < kSmokeGrow + kSmokeHold) {
            e.aoeRadius = kSmokeMaxR;
        } else if (t < kSmokeGrow + kSmokeHold + kSmokeShrink) {
            const float s = (t - kSmokeGrow - kSmokeHold) / kSmokeShrink;
            e.aoeRadius = kSmokeMaxR * (1.0f - s);
        } else {
            // Cycle done — clear the cloud and wait out the gap before re-arming.
            e.aoeRadius = 0.0f;
            e.state = SmokeApproach;
            e.stateTimer = 0.0f;
        }
        // Chip damage while the player stands inside the cloud, rate-limited via
        // attackCooldown (decayed once per frame in updateEnemies).
        if (e.aoeRadius > 0.0f && dist < e.aoeRadius && e.attackCooldown <= 0.0f) {
            damagePlayer(gs, ctx, kSmokeChip);
            gs.playerHitThisFrame = true;
            e.attackCooldown = kSmokeChipGap;
        }
    }
}

}  // namespace

void updateEnemies(GameState& gs, const GameContext& ctx, float dt) {
    int living = 0;
    for (Enemy& e : gs.enemies.list) {
        if (!e.alive) continue;
        // Death: combat sets hp; we react and skip.
        if (e.hp <= 0.0f) { e.alive = false; continue; }

        // Per-frame timer decay shared by both archetypes.
        if (e.attackCooldown > 0.0f) e.attackCooldown -= dt;
        if (e.hitFlash > 0.0f) e.hitFlash -= dt;

        switch (e.type) {
            case EnemyType::Kipuchka: updateKipuchka(gs, ctx, e, dt); break;
            case EnemyType::Smoker:   updateSmoker(gs, ctx, e, dt);   break;
        }
        ++living;
    }
    gs.enemies.alive = living;
}

void collectEnemyDraws(const GameState& gs, DrawList& out) {
    for (const Enemy& e : gs.enemies.list) {
        if (!e.alive) continue;

        if (e.type == EnemyType::Kipuchka) {
            DrawInstance d;
            d.mesh = MeshId::Kipuchka;
            d.pos = e.pos;
            d.yaw = e.yaw;
            d.scale = {kKipScale, kKipScale, kKipScale};
            Vec3 tint{1.0f, 1.0f, 1.0f};
            // Telegraph: bleed toward red as the windup peaks.
            if (e.telegraph > 0.0f) {
                const float u = clampf(e.telegraph / kKipWindup, 0.0f, 1.0f);
                tint = lerp3(tint, Vec3{1.6f, 0.25f, 0.25f}, u);
            }
            // Hit flash: overbright white on taking damage.
            if (e.hitFlash > 0.0f) {
                const float u = clampf(e.hitFlash / kHitFlash, 0.0f, 1.0f);
                tint = lerp3(tint, Vec3{2.0f, 2.0f, 2.0f}, u);
            }
            d.tint = tint;
            out.push_back(d);
        } else {  // Smoker
            DrawInstance d;
            d.mesh = MeshId::Smoker;
            d.pos = e.pos;
            d.yaw = e.yaw;
            d.scale = {kSmokeScaleXZ, kSmokeScaleY, kSmokeScaleXZ};
            Vec3 tint{1.0f, 1.0f, 1.0f};
            if (e.aoeWarn) tint = {1.0f, 0.7f, 0.5f};  // warm pre-warm glow
            if (e.hitFlash > 0.0f) {
                const float u = clampf(e.hitFlash / kHitFlash, 0.0f, 1.0f);
                tint = lerp3(tint, Vec3{2.0f, 2.0f, 2.0f}, u);
            }
            d.tint = tint;
            out.push_back(d);

            // Pre-warm ring: a flat red disc at the eventual cloud footprint.
            if (e.aoeWarn) {
                DrawInstance ring;
                ring.mesh = MeshId::SmokeCloud;
                ring.pos = {e.pos.x, 0.05f, e.pos.z};
                ring.scale = {kSmokeMaxR, 0.1f, kSmokeMaxR};
                ring.tint = {0.9f, 0.35f, 0.3f};  // danger colour
                out.push_back(ring);
            }
            // Live cloud: dim grey puff scaled by the current radius. The
            // renderer draws SmokeCloud as an unlit billboard-ish puff; the dark
            // tint + large scale reads as semi-transparent smoke.
            if (e.aoeRadius > 0.0f) {
                DrawInstance cloud;
                cloud.mesh = MeshId::SmokeCloud;
                cloud.pos = {e.pos.x, e.aoeRadius * 0.5f, e.pos.z};
                cloud.scale = {e.aoeRadius, e.aoeRadius, e.aoeRadius};
                cloud.tint = {0.55f, 0.55f, 0.62f};
                out.push_back(cloud);
            }
        }
    }
}

}  // namespace rv_3dmppc
