// Console input state — one gamepad's worth of controls, sampled once per frame.
//
// The mppc console has 1–2 gamepads and no mouse/keyboard (see
// docs/platform/specs.md). This struct models a single gamepad in gameplay
// terms; the console layer (platform/console) fills it from an SDL gamepad, or
// from a keyboard fallback so the template is playable on a plain desktop.
//
// SHARED CONTRACT — owned by the orchestrator. Console (Agent A) *produces* it;
// game systems (Agents B–E) *consume* it. Field names are stable: extend by
// adding, never rename/remove.
#pragma once

#include "math/math.hpp"

namespace rv_3dmppc {

// Edge-tracked button: `held` is level, `pressed`/`released` are one-frame edges.
struct Button {
    bool held = false;
    bool pressed = false;
    bool released = false;

    // Console calls this each frame with the raw level to derive the edges.
    void update(bool nowHeld) {
        pressed = nowHeld && !held;
        released = !nowHeld && held;
        held = nowHeld;
    }
};

struct InputState {
    Vec2 moveAxis{};  // left stick: x=strafe (+right), y=forward (+fwd), [-1,1]
    Vec2 lookAxis{};  // right stick: x=yaw (+right), y=pitch (+up), [-1,1]
    Vec2 lookDelta{}; // mouse look: this-frame yaw/pitch delta in radians
                      // (x=+right, y=+up). Absolute — applied without dt scaling.

    Button throwBrick;  // primary face button — ready/throw brick
    Button meleePipe;   // secondary face button — pipe swing
    Button interact;    // interact / hold (ritual, pickups)
    Button context;     // context action / weapon swap
    Button start;       // pause / start

    bool quit = false;  // window close or Esc — console requests shutdown
};

}  // namespace rv_3dmppc
