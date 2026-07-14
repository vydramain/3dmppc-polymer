#include "solidmaid.hpp"

#include "areas.hpp"
#include "combat.hpp"
#include "dayloop.hpp"
#include "enemies.hpp"
#include "hud.hpp"
#include "player.hpp"
#include "ritual.hpp"
#include "spawner.hpp"
#include "weapons.hpp"

namespace rv_3dmppc {

namespace {

// Per-area atmosphere / clear color, darkened as world corruption climbs.
Color atmosphere(const GameState& gs) {
    Vec3 base{0.09f, 0.086f, 0.16f};  // deep PSX blue-grey (Home)
    switch (gs.world.current) {
        case Area::Home:
            base = {0.10f, 0.09f, 0.13f};
            break;
        case Area::Street:
            base = {0.13f, 0.13f, 0.16f};
            break;
        case Area::Factory:
            base = {0.10f, 0.08f, 0.07f};
            break;
    }
    const float murk = clampf(gs.loop.corruption * 0.04f, 0.0f, 0.5f);
    base = base * (1.0f - murk);
    return fromVec3(base);
}

bool inPlayablePhase(Phase p) {
    return p == Phase::Home || p == Phase::Street || p == Phase::Factory;
}

}  // namespace

void SolidMaidDisc::boot(const rv_DiscServices& services) {
    ctx_.audio = services.audio;
    ctx_.save = services.save;
    renderer_.init();
    bootDayLoop(state_, ctx_);
}

void SolidMaidDisc::update(const InputState& in, float dt) {
    if (in.quit) quit_ = true;

    state_.time += dt;
    state_.prompt = nullptr;
    state_.playerHitThisFrame = false;

    // Day loop drives phase/area transitions, summary, mutation, save, gameover.
    updateDayLoop(state_, ctx_, in, dt);

    if (inPlayablePhase(state_.loop.phase)) {
        updatePlayer(state_, ctx_, in, dt);
        updateWeapons(state_, ctx_, in, dt);
        updateSpawner(state_, ctx_, dt);
        updateEnemies(state_, ctx_, dt);
        updateRitual(state_, ctx_, in, dt);
        updateCombat(state_, ctx_, dt);  // may interrupt ritual / apply hitstop
        updateAreas(state_, ctx_, dt);
    }
}

void SolidMaidDisc::render(rv_Framebuffer& fb) {
    const Camera cam = playerCamera(state_);

    DrawList list;
    list.reserve(256);
    collectAreaDraws(state_, list);
    collectRitualDraws(state_, list);
    collectEnemyDraws(state_, list);
    collectWeaponDraws(state_, list);  // held item + live brick projectiles

    renderer_.draw(list, cam, fb, atmosphere(state_));
    drawHud(state_, fb);
    if (debugOverlay_) drawDebugOverlay(state_, fb, lastFrameMs_);
}

}  // namespace rv_3dmppc
