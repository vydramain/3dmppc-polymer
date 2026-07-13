// Enemy spawner discipline — Agent C. Caps active enemies, keeps a minimum
// distance from the player, and honours a spawn grace so encounters stay fair
// and the frame stays stable (mechanics.md). Deterministic: ctx.rng only.
#include "game/spawner.hpp"

#include <vector>

#include "game/context.hpp"
#include "game/game_state.hpp"

namespace rv_3dmppc {

namespace {

// Type-appropriate spawn stats.
constexpr float kKipuchkaHp = 12.0f;
constexpr float kSmokerHp   = 20.0f;
constexpr float kSpawnGrace = 1.5f;  // fresh-spawn attack grace (can't insta-hit)
constexpr float kKipuchkaBias = 0.6f;  // fraction of spawns that roll Kipuchka

int countLiving(const GameState& gs) {
    int n = 0;
    for (const Enemy& e : gs.enemies.list) if (e.alive) ++n;
    return n;
}

float xzDist(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

}  // namespace

void beginEncounter(GameState& gs, int budget, const SpawnConfig& cfg) {
    gs.spawner.enabled = true;
    gs.spawner.budget = budget;
    gs.spawner.cfg = cfg;
    gs.spawner.sinceLast = 0.0f;
}

void endEncounter(GameState& gs) { gs.spawner.enabled = false; }

void updateSpawner(GameState& gs, const GameContext& ctx, float dt) {
    SpawnerState& sp = gs.spawner;
    if (!sp.enabled) return;

    const int living = countLiving(gs);

    // Budget exhausted: nothing more to spawn. Auto-close once the field clears.
    if (sp.budget == 0) {
        if (living == 0) endEncounter(gs);
        return;
    }

    sp.sinceLast += dt;
    // Respect both the target interval and the hard grace between spawns.
    if (sp.sinceLast < sp.cfg.interval || sp.sinceLast < sp.cfg.grace) return;
    if (living >= sp.cfg.cap) return;  // cap active enemies (~5)
    if (sp.points.empty()) return;

    // Gather spawn points that sit at least minDist from the player.
    std::vector<int> valid;
    valid.reserve(sp.points.size());
    for (int i = 0; i < static_cast<int>(sp.points.size()); ++i) {
        if (xzDist(sp.points[i], gs.player.pos) >= sp.cfg.minDist) valid.push_back(i);
    }
    if (valid.empty()) return;  // no fair point right now — retry next frame

    const int pick = valid[ctx.rng.rangeI(0, static_cast<int>(valid.size()) - 1)];
    // rng-weighted type choice (biased toward the melee pest).
    const EnemyType type =
        ctx.rng.unit() < kKipuchkaBias ? EnemyType::Kipuchka : EnemyType::Smoker;

    spawnEnemyAt(gs, type, sp.points[pick]);

    if (sp.budget > 0) --sp.budget;  // -1 = endless: never decrements
    sp.sinceLast = 0.0f;             // reset grace/interval
}

void spawnEnemyAt(GameState& gs, EnemyType type, const Vec3& pos) {
    Enemy e;
    e.type = type;
    e.pos = pos;
    e.alive = true;
    e.hp = (type == EnemyType::Kipuchka) ? kKipuchkaHp : kSmokerHp;
    e.attackCooldown = kSpawnGrace;  // brief grace so a fresh spawn can't insta-hit
    gs.enemies.list.push_back(e);
}

}  // namespace rv_3dmppc
