# Mechanics

First-person, tactile, and readable. Two weapons, a short ritual, and enemies
with strong telegraphs. Tuned for the console: gamepad input, a software
rasterizer, and a stable frame.

## Controls (gamepad — console re-target)

The mppc console has **1–2 gamepads and no mouse/keyboard**. The original Godot
prototype was built around mouse-look; on the console, first-person aiming moves
to the **right stick**. This is the single biggest input change from the source
design.

- **Left stick:** move.
- **Right stick:** look/aim (with a sane sensitivity and pitch clamp; consider
  light aim-assist/gravity toward targets to keep stick-aiming fair).
- **Face buttons:** throw brick (primary), pipe melee (secondary), interact/hold
  (ritual, pickups), and a context action.
- **Optional:** sprint / crouch if they earn their place — the slice works
  without them.
- Light head-bob and a subtle low-HP vignette (via palette/dither, not shaders).
  Keep bob subtle and, ideally, adjustable — motion comfort matters in first
  person.

## Weapons

- **Brick (throwable — primary).** Hold to ready, release to throw on an arc;
  cooldown between throws. Physics arc, bounce, and a satisfying impact. Spare
  bricks are scattered in the world so the player is never hard-gated. Optional:
  a short-cooldown recall so a thrown brick returns to hand.
- **Pipe (melee — backup / gap-maker).** Short, fast swing with a clear
  telegraph and cooldown; primarily used to create space for safer throws and to
  finish weakened enemies.
- **Pillars (ranged — unlock, post-MVP).** After the first lamppost is assembled,
  unlock a ranged option. Two candidate designs; prototype before committing:
  - **A — Emitter:** assembled lampposts emit a periodic ranged blast the player
    can trigger while nearby.
  - **B — Piece-weapon:** a lamppost component becomes a limited-ammo ranged tool
    after first assembly.
  Keep this **out of the MVP** until one prototype proves fun and cheap.

## Combat feel

Weight is everything, and it's mostly cheap tricks stacked well:

- **Hitstop** (~0.06–0.1s) on solid hits.
- **Camera micro-shake** on impact.
- **Loud, punchy SFX** (see [`art-and-audio.md`](art-and-audio.md)).
- **Readable telegraphs:** enemy windups **≥ ~300 ms**, low visual clutter, one
  signature tell per attack. The software rasterizer and first-person view demand
  legibility over detail.

## Enemies (behaviour)

See [`characters.md`](characters.md) for fiction; the gameplay hooks:

- **Kipuchka** — fast, jittery melee; on repeated hits can **stun + steal the
  pillar component** and flee to an alley. Recover by chasing it down. (Ship a
  no-steal stub first; add the steal later.)
- **Midnight Smoker** — expanding **smoke-cloud AoE** with a pre-warm ring;
  reduces visibility and deals chip damage. Area denial.
- **Spawner discipline:** cap ~5 active enemies, enforce a minimum spawn distance
  from the player, and a ~1.5s spawn grace. Keeps the frame stable and the
  encounter fair.

## The ritual / assembly (Factory)

The core "under pressure" interaction:

- **3-step hold-to-assemble** the lamppost, each step a hold-interaction with a
  progress bar.
- **Interrupt on damage** — taking a hit interrupts the current step, so the
  player must clear space (pipe) before committing.
- Escalate the encounter once (around step 2). On completion, spawn the **return
  trigger** home and record that a new pillar was created.

## Loop & fail states

- **Win:** complete the factory ritual and return home once.
- **Lose:** knocked out anywhere → restart the day.
- **Persistence:** loop flags (day count, world corruption, home mutations) are
  small and fit the memory card (~128 KB) — see [`content.md`](content.md).

## MVP acceptance (feel checks)

- A new player completes a full loop in **≤ 12 minutes** without reading anything.
- Hits feel punchy: SFX + micro-hitstop + shake land together.
- Enemy telegraphs are unmistakable; failure is readable and fair.
- The frame stays stable through spawns, the AoE cloud, and assembly.
