// Agent D — the Factory lamppost ritual: a 3-step hold-to-assemble under
// pressure. Holding [Interact] near the socket fills a step; a hit interrupts it
// (combat calls interruptRitual). The encounter escalates once around step 2;
// on completion the return-home trigger is armed (by the day loop).
#include "ritual.hpp"

#include <cmath>

#include "core/audio.hpp"
#include "context.hpp"
#include "game_state.hpp"
#include "spawner.hpp"

namespace rv_3dmppc {

namespace {
constexpr float kReachRadius = 2.0f;  // metres from the socket to assemble
constexpr int kEscalationWave = 4;    // extra enemies spawned at step 2
}  // namespace

void initRitual(GameState& gs, const Vec3& socketPos) {
    gs.ritual = RitualState{};
    gs.ritual.socketPos = socketPos;
    gs.ritual.available = false;
}

void updateRitual(GameState& gs, const GameContext& ctx, const InputState& in, float dt) {
    RitualState& r = gs.ritual;

    // Only meaningful in the Factory.
    if (gs.world.current != Area::Factory) {
        r.available = false;
        r.active = false;
        return;
    }

    // Availability = within reach of the socket (XZ distance).
    const Vec3 d = gs.player.pos - r.socketPos;
    const float distXZ = std::sqrt(d.x * d.x + d.z * d.z);
    r.available = distXZ <= kReachRadius;

    if (r.complete) {
        r.active = false;
        return;
    }

    if (r.available && in.interact.held) {
        r.active = true;
        r.progress += dt / RitualState::kStepSeconds;
        if (r.progress >= 1.0f) {
            r.progress = 0.0f;
            r.step += 1;
            if (ctx.audio) ctx.audio->playSfx(Audio::Sfx::RitualStep);

            // Escalate once, when the 2nd step lands.
            if (r.step == 2 && !r.escalated) {
                r.escalated = true;
                SpawnConfig cfg;
                cfg.cap = 5;
                cfg.minDist = 6.0f;
                cfg.grace = 1.5f;
                beginEncounter(gs, kEscalationWave, cfg);
            }
            // Final step done → ritual complete.
            if (r.step >= RitualState::kSteps) {
                r.complete = true;
                r.active = false;
                if (ctx.audio) ctx.audio->playSfx(Audio::Sfx::RitualComplete);
            }
        }
        if (r.available && !r.complete && !gs.prompt)
            gs.prompt = "Hold [Interact]: assemble";
    } else {
        // Not holding → stop; the step's progress holds until interrupted.
        r.active = false;
        if (r.available && !gs.prompt) gs.prompt = "Hold [Interact]: assemble";
    }
}

void collectRitualDraws(const GameState& gs, DrawList& out) {
    const RitualState& r = gs.ritual;
    if (gs.world.current != Area::Factory) return;

    // The socket base is always present at the arena centre.
    DrawInstance base;
    base.mesh = MeshId::LamppostSocket;
    base.pos = r.socketPos;
    base.scale = {0.6f, 0.4f, 0.6f};
    out.push_back(base);

    // Partial → full lamppost rises with completed steps (+ the in-progress one).
    const float frac =
        clampf((r.step + r.progress) / static_cast<float>(RitualState::kSteps),
               0.0f, 1.0f);
    if (frac > 0.0f) {
        DrawInstance pillar;
        pillar.mesh = MeshId::Lamppost;
        const float h = lerpf(0.4f, 3.2f, frac);  // grows as it assembles
        pillar.pos = {r.socketPos.x, r.socketPos.y + h * 0.5f, r.socketPos.z};
        pillar.scale = {0.3f, h, 0.3f};
        pillar.tint = r.complete ? Vec3{1.0f, 0.95f, 0.7f}   // lit when done
                                 : Vec3{0.7f, 0.7f, 0.8f};
        out.push_back(pillar);
    }
}

void interruptRitual(GameState& gs) {
    // Losing the current step's progress is the cost of taking a hit.
    gs.ritual.active = false;
    gs.ritual.progress = 0.0f;
}

}  // namespace rv_3dmppc
