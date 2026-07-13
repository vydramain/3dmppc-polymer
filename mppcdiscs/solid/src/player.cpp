// Agent B — first-person movement/look, camera feel (head-bob, micro-shake,
// hitstop) and player damage. Data-oriented free functions over GameState.
#include "player.hpp"

#include <cmath>

#include "context.hpp"
#include "game_state.hpp"

namespace rv_3dmppc {

namespace {

// ---- feel constants (tuned for gamepad stick-look on the 320x240 frame) -----
constexpr float kTurnSpeed  = 2.6f;    // rad/s at full stick (~150 deg/s)
constexpr float kWalkSpeed  = 3.0f;    // m/s ground move (mechanics.md walk)
constexpr float kPitchClamp = 1.3f;    // rad — keep from gimballing straight up/down
constexpr float kEyeHeight  = 1.6f;    // m — hold the eye at standing height
constexpr float kShakeDecay = 0.55f;   // per-second linear decay of shake magnitude
constexpr float kBobAmp     = 0.035f;  // m — vertical head-bob (kept subtle: comfort)
constexpr float kStrideLen  = 0.75f;   // m per footstep (also drives bob timing)
const     float kBobFreq    = kPi / kStrideLen;  // rad/m: one bob down-beat per stride
constexpr float kHitstopTime = 0.08f;  // s freeze on a solid hit (mechanics: 0.06-0.1)
constexpr float kHurtShake   = 0.09f;  // ~9 cm punch of camera shake on being hit

// Camera-forward projected onto the ground plane (movement basis).
Vec3 flatForward(float yaw) {
    return {std::sin(yaw), 0.0f, std::cos(yaw)};  // +z is into the screen (LH)
}
// Camera-right (matches Camera::right()).
Vec3 flatRight(float yaw) {
    return {std::cos(yaw), 0.0f, -std::sin(yaw)};
}

// Symmetric jitter in [-1,1] from the deterministic RNG.
float jitter(const GameContext& ctx) { return ctx.rng.unit() * 2.0f - 1.0f; }

}  // namespace

void updatePlayer(GameState& gs, const GameContext& ctx, const InputState& in, float dt) {
    PlayerState& p = gs.player;

    // Camera shake decays and is re-sampled every frame so the impact "punch"
    // keeps ringing even while a hitstop freeze holds the sim still.
    p.shakeMag -= kShakeDecay * dt;
    if (p.shakeMag < 0.0f) p.shakeMag = 0.0f;
    p.shake = {jitter(ctx) * p.shakeMag, jitter(ctx) * p.shakeMag, 0.0f};

    // Hitstop: freeze motion for weight, then bleed the timer down.
    if (p.hitstop > 0.0f) {
        p.hitstop -= dt;
        if (p.hitstop < 0.0f) p.hitstop = 0.0f;
        return;  // no look/move this frame — the freeze is the game-feel
    }

    // ---- look (right stick + mouse) -----------------------------------------
    // Stick/keys are a held rate (scaled by dt); the mouse delta is an absolute
    // this-frame amount already in radians, so it is added straight in.
    p.yaw += in.lookAxis.x * kTurnSpeed * dt + in.lookDelta.x;
    p.pitch += in.lookAxis.y * kTurnSpeed * dt + in.lookDelta.y;
    p.pitch = clampf(p.pitch, -kPitchClamp, kPitchClamp);

    // ---- move (left stick, relative to yaw) ---------------------------------
    const Vec3 fwd = flatForward(p.yaw);
    const Vec3 rgt = flatRight(p.yaw);
    Vec3 wish = rgt * in.moveAxis.x + fwd * in.moveAxis.y;
    float wishLen = std::sqrt(wish.x * wish.x + wish.z * wish.z);
    if (wishLen > 1.0f) {  // clamp diagonal to unit so strafing isn't faster
        wish = wish * (1.0f / wishLen);
        wishLen = 1.0f;
    }

    const float speed = wishLen * kWalkSpeed;   // m/s this frame
    p.velocity = {wish.x * kWalkSpeed, 0.0f, wish.z * kWalkSpeed};
    const Vec3 disp = {p.velocity.x * dt, 0.0f, p.velocity.z * dt};

    // Axis-separated push-out against world AABBs, keeping `radius` clearance.
    // Resolve X, then Z: each collider is inflated by the player radius and the
    // point is snapped back out along whichever axis it just penetrated.
    p.pos.x += disp.x;
    for (const AABB& c : gs.world.colliders) {
        const float minx = c.min.x - p.radius, maxx = c.max.x + p.radius;
        const float minz = c.min.z - p.radius, maxz = c.max.z + p.radius;
        if (p.pos.x > minx && p.pos.x < maxx && p.pos.z > minz && p.pos.z < maxz) {
            p.pos.x = (disp.x > 0.0f) ? minx : maxx;
        }
    }
    p.pos.z += disp.z;
    for (const AABB& c : gs.world.colliders) {
        const float minx = c.min.x - p.radius, maxx = c.max.x + p.radius;
        const float minz = c.min.z - p.radius, maxz = c.max.z + p.radius;
        if (p.pos.x > minx && p.pos.x < maxx && p.pos.z > minz && p.pos.z < maxz) {
            p.pos.z = (disp.z > 0.0f) ? minz : maxz;
        }
    }
    p.pos.y = kEyeHeight;  // no verticality in the slice — pin the eye height

    // ---- head-bob + footsteps -----------------------------------------------
    p.bobPhase += speed * dt;             // advance by distance travelled
    if (speed > 0.05f) {
        p.distSinceStep += speed * dt;
        if (p.distSinceStep >= kStrideLen) {
            p.distSinceStep -= kStrideLen;
            if (ctx.audio) ctx.audio->playSfx(Audio::Sfx::Footstep);
        }
    } else {
        p.distSinceStep = 0.0f;           // reset so a stop doesn't half-cock a step
    }
}

Camera playerCamera(const GameState& gs) {
    const PlayerState& p = gs.player;
    Camera c;
    const float bob = std::sin(p.bobPhase * kBobFreq) * kBobAmp;  // subtle vertical
    c.pos = p.pos + Vec3{0.0f, bob, 0.0f} + p.shake;
    c.yaw = p.yaw;
    c.pitch = p.pitch;
    c.fovYRadians = radians(70.0f);       // comfortable FP fov
    c.aspect = 320.0f / 240.0f;           // console native framebuffer
    return c;
}

void damagePlayer(GameState& gs, const GameContext& ctx, float amount) {
    PlayerState& p = gs.player;
    if (!p.alive) return;
    p.hp = clampf(p.hp - amount, 0.0f, p.maxHp);
    if (p.hp <= 0.0f) {
        p.hp = 0.0f;
        p.alive = false;
    }
    // Feel: freeze + shake + loud hurt SFX all land together (mechanics.md).
    p.hitstop = kHitstopTime;
    p.shakeMag = kHurtShake;
    if (ctx.audio) ctx.audio->playSfx(Audio::Sfx::PlayerHurt);
}

}  // namespace rv_3dmppc
