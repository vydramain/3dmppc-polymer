// Agent D — the day loop: Home → Street → Factory → Return, with a day summary,
// one Home mutation per survived loop, and persistence to the memory card.
// Win = complete the ritual and return home; Lose = knocked out → restart day.
#include "dayloop.hpp"

#include "core/audio.hpp"
#include "areas.hpp"
#include "context.hpp"
#include "game_state.hpp"
#include "ritual.hpp"
#include "spawner.hpp"

namespace rv_3dmppc {

namespace {
constexpr float kSummarySeconds = 4.0f;  // auto-advance the day summary
constexpr int kMaxHomeMutations = 8;     // cap on visible Home clutter

void moveToSpawn(GameState& gs) {
    gs.player.pos = gs.world.playerSpawn;
    gs.player.yaw = gs.world.playerSpawnYaw;
}

// A handful of enemies over the commute.
void startStreetEncounter(GameState& gs) {
    SpawnConfig cfg;
    cfg.cap = 5;
    cfg.minDist = 6.0f;
    cfg.grace = 1.5f;
    beginEncounter(gs, 6, cfg);
}

// Reset the player + field for a clean day (restart or new day).
void resetForFreshDay(GameState& gs) {
    gs.player.hp = gs.player.maxHp;
    gs.player.alive = true;
    gs.enemies.list.clear();
    gs.enemies.alive = 0;
    gs.projectiles.clear();
    endEncounter(gs);
}
}  // namespace

void bootDayLoop(GameState& gs, const GameContext& ctx) {
    gs.loop = DayLoopState{};

    // Load persisted loop flags if the card holds a valid blob.
    SaveBlob blob;
    const SaveBlob fresh{};
    if (ctx.save && ctx.save->read(&blob, sizeof(blob)) &&
        blob.magic == fresh.magic && blob.version == fresh.version) {
        gs.loop.day = blob.day;
        gs.loop.corruption = blob.corruption;
        gs.loop.homeMutations = blob.homeMutations;
        gs.loop.loopsCompleted = blob.loopsCompleted;
    }

    gs.loop.phase = Phase::Home;
    gs.loop.started = true;
    loadArea(gs, ctx, Area::Home);
    moveToSpawn(gs);
}

void updateDayLoop(GameState& gs, const GameContext& ctx, const InputState& in, float dt) {
    DayLoopState& L = gs.loop;

    // Death anywhere in play → game over. (Return has no combat.)
    if (!gs.player.alive && L.phase != Phase::GameOver && L.phase != Phase::Return) {
        L.phase = Phase::GameOver;
        endEncounter(gs);
        if (ctx.audio) ctx.audio->setMood(Audio::Mood::Defeat);
        return;
    }

    switch (L.phase) {
        case Phase::Home:
            if (playerAtExit(gs)) {
                L.phase = Phase::Street;
                loadArea(gs, ctx, Area::Street);
                moveToSpawn(gs);
                startStreetEncounter(gs);
            }
            break;

        case Phase::Street:
            if (playerAtExit(gs)) {
                endEncounter(gs);
                L.phase = Phase::Factory;
                loadArea(gs, ctx, Area::Factory);
                initRitual(gs, gs.world.socketPos);
                moveToSpawn(gs);
                // No factory encounter yet — the ritual escalation starts it.
            }
            break;

        case Phase::Factory:
            if (gs.ritual.complete) gs.world.exitArmed = true;  // arm return
            if (gs.ritual.complete && playerAtExit(gs)) {
                endEncounter(gs);
                L.phase = Phase::Return;
                L.showSummary = true;
                L.summaryTimer = kSummarySeconds;
                if (ctx.audio) ctx.audio->setMood(Audio::Mood::Victory);
            }
            break;

        case Phase::Return: {
            L.summaryTimer -= dt;
            const bool advance = L.summaryTimer <= 0.0f ||
                                 in.interact.pressed || in.start.pressed;
            if (advance) {
                // A day survived: bank the loop, mutate Home, persist.
                L.loopsCompleted += 1;
                L.day += 1;
                L.corruption += 1;
                if (L.homeMutations < kMaxHomeMutations) L.homeMutations += 1;

                SaveBlob blob;
                blob.day = L.day;
                blob.corruption = L.corruption;
                blob.homeMutations = L.homeMutations;
                blob.loopsCompleted = L.loopsCompleted;
                if (ctx.save) ctx.save->write(&blob, sizeof(blob));

                // Fresh day back Home (loadArea sets the Home mood).
                L.showSummary = false;
                resetForFreshDay(gs);
                L.phase = Phase::Home;
                loadArea(gs, ctx, Area::Home);
                moveToSpawn(gs);
            }
            break;
        }

        case Phase::GameOver:
            if (in.start.pressed || in.interact.pressed) {
                // Restart the SAME day; loop flags persist (world stays strange).
                resetForFreshDay(gs);
                L.phase = Phase::Home;
                loadArea(gs, ctx, Area::Home);
                moveToSpawn(gs);
            }
            break;
    }
}

}  // namespace rv_3dmppc
