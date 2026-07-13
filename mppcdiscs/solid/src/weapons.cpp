// Agent B — improvised weapons: brick (charged throwable primary) and pipe
// (fast melee backup). Spawns projectiles into GameState; combat (Agent C)
// integrates + resolves them. Also builds the first-person held-item draws.
#include "weapons.hpp"

#include <cmath>

#include "context.hpp"
#include "game_state.hpp"

namespace rv_3dmppc {

namespace {

// ---- feel constants ---------------------------------------------------------
constexpr float kChargeTime   = 0.7f;   // s to charge a throw from 0 -> full
constexpr float kMinChargeGo  = 0.15f;  // below this the release is a no-throw tap
constexpr float kThrowMin     = 8.0f;   // m/s throw speed at min charge
constexpr float kThrowMax     = 16.0f;  // m/s throw speed at full charge (weighty)
constexpr float kThrowArcUp   = 3.0f;   // m/s upward kick so the brick lobs on an arc
constexpr float kBrickCooldown = 0.6f;  // s between throws
constexpr float kBrickLife    = 4.0f;   // s before an unspent brick despawns
constexpr float kPipeSwingTime = 0.35f; // s full swing animation
constexpr float kPipeCooldown  = 0.5f;  // s between swings
constexpr float kPipeHitStart  = 0.1f;  // s into the swing the head starts to connect
constexpr float kPipeHitEnd    = 0.2f;  // s into the swing the active window closes
constexpr int   kMaxBrickAmmo  = 7;     // sane carry cap (mechanics: 5-9 range)

// Full camera-forward including pitch (matches Camera::forward()).
Vec3 camForward(float yaw, float pitch) {
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    return {sy * cp, sp, cy * cp};
}
Vec3 camRight(float yaw) {
    return {std::cos(yaw), 0.0f, -std::sin(yaw)};
}
float decay(float v, float dt) { v -= dt; return v < 0.0f ? 0.0f : v; }

}  // namespace

void updateWeapons(GameState& gs, const GameContext& ctx, const InputState& in, float dt) {
    WeaponState& w = gs.weapons;
    const PlayerState& p = gs.player;

    // Cooldowns and the swing timer always tick.
    w.brickCooldown = decay(w.brickCooldown, dt);
    w.pipeCooldown = decay(w.pipeCooldown, dt);
    w.pipeSwing = decay(w.pipeSwing, dt);

    // Weapon swap (context button). A swap cancels any pending brick charge.
    if (in.context.pressed) {
        w.active = (w.active == WeaponState::Brick) ? WeaponState::Pipe : WeaponState::Brick;
        w.brickHeld = false;
        w.brickCharge = 0.0f;
        if (ctx.audio) ctx.audio->playSfx(Audio::Sfx::UiClick);
    }

    // ---- BRICK (primary): hold to charge, release to throw ------------------
    if (w.active == WeaponState::Brick) {
        if (in.throwBrick.held) {
            w.brickHeld = true;
            w.brickCharge = clampf(w.brickCharge + dt / kChargeTime, 0.0f, 1.0f);
        }
        if (in.throwBrick.released && w.brickHeld) {
            const bool canThrow = w.brickCharge > kMinChargeGo &&
                                  w.brickCooldown <= 0.0f && w.brickAmmo > 0;
            if (canThrow) {
                const Vec3 fwd = camForward(p.yaw, p.pitch);
                const float speed = lerpf(kThrowMin, kThrowMax, w.brickCharge);
                Projectile pr;
                pr.pos = p.pos + fwd * 0.4f;                    // leave the hand
                pr.vel = fwd * speed + Vec3{0.0f, kThrowArcUp, 0.0f};  // slight up-arc
                pr.life = kBrickLife;
                pr.active = true;
                pr.kind = 0;                                    // 0 = brick
                gs.projectiles.push_back(pr);
                w.brickCooldown = kBrickCooldown;
                w.brickAmmo -= 1;
                if (ctx.audio) ctx.audio->playSfx(Audio::Sfx::BrickThrow);
            }
            w.brickCharge = 0.0f;
            w.brickHeld = false;
        }
    } else {
        // Not the active weapon — make sure no charge lingers.
        w.brickCharge = 0.0f;
        w.brickHeld = false;
    }

    // ---- PIPE (secondary): fast telegraphed swing ---------------------------
    if (w.active == WeaponState::Pipe && in.meleePipe.pressed &&
        w.pipeCooldown <= 0.0f && w.pipeSwing <= 0.0f) {
        w.pipeSwing = kPipeSwingTime;
        w.pipeCooldown = kPipeCooldown;
        if (ctx.audio) ctx.audio->playSfx(Audio::Sfx::PipeSwing);
    }
    // Active window: the head only connects mid-swing so the wind-up telegraphs.
    const float elapsed = kPipeSwingTime - w.pipeSwing;  // pipeSwing counts down
    w.pipeHitActive = w.pipeSwing > 0.0f &&
                      elapsed >= kPipeHitStart && elapsed <= kPipeHitEnd;
}

void collectWeaponDraws(const GameState& gs, DrawList& out) {
    const WeaponState& w = gs.weapons;
    const PlayerState& p = gs.player;

    // View-space basis so the held item hangs at a fixed spot on screen.
    const Vec3 fwd = camForward(p.yaw, p.pitch);
    const Vec3 rgt = camRight(p.yaw);
    const Vec3 up = cross(fwd, rgt);   // true up (LH: y = z x x)

    // Pick the held mesh: empty hand if the brick is spent.
    MeshId held = MeshId::ViewHand;
    if (w.active == WeaponState::Brick) {
        held = w.brickAmmo > 0 ? MeshId::ViewBrick : MeshId::ViewHand;
    } else {
        held = MeshId::ViewPipe;
    }

    // Rest pose: bottom-right of the view, a short reach ahead of the eye.
    Vec3 pos = p.pos + fwd * 0.5f + rgt * 0.28f + up * (-0.26f);
    float roll = 0.0f;

    // Juice: charging pulls the brick back and up (wind-up); swinging arcs the pipe.
    if (w.active == WeaponState::Brick && w.brickCharge > 0.0f) {
        pos = pos + fwd * (-0.12f * w.brickCharge) + up * (0.10f * w.brickCharge);
    } else if (w.pipeSwing > 0.0f) {
        const float t = (kPipeSwingTime - w.pipeSwing) / kPipeSwingTime;  // 0..1
        const float arc = std::sin(t * kPi);                              // 0..1..0
        pos = pos + fwd * (0.18f * arc) + up * (0.10f * arc) + rgt * (-0.14f * arc);
        roll = -1.0f * arc;   // rad — swipe the pipe across the view
    }

    DrawInstance item;
    item.mesh = held;
    item.pos = pos;
    item.yaw = p.yaw;
    item.pitch = p.pitch;
    item.roll = roll;
    item.scale = {0.15f, 0.15f, 0.15f};
    out.push_back(item);

    // Live thrown bricks — one draw per active projectile, tumbling for weight.
    for (const Projectile& pr : gs.projectiles) {
        if (!pr.active) continue;
        DrawInstance d;
        d.mesh = MeshId::Brick;
        d.pos = pr.pos;
        d.yaw = pr.life * 6.0f;    // tumble from its remaining life (cheap spin)
        d.pitch = pr.life * 4.0f;
        d.scale = {0.2f, 0.2f, 0.2f};
        out.push_back(d);
    }
}

void addBrickAmmo(GameState& gs, int n) {
    int ammo = gs.weapons.brickAmmo + n;
    if (ammo > kMaxBrickAmmo) ammo = kMaxBrickAmmo;
    if (ammo < 0) ammo = 0;
    gs.weapons.brickAmmo = ammo;
}

}  // namespace rv_3dmppc
