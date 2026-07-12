# Overview

## Title

**Solidmaid: Alkoldun Vasiliusavich**

## Elevator pitch

Wake up in a cramped Khrushchyovka apartment. Step into the corridor, walk the
uncanny street, endure a shift at the factory, and get home before it gets
worse. Alkoldun rituals seep into the mundane. Improvise with bricks and pipes,
perform a short rite under pressure, and each loop the world mutates to mirror a
fraying psyche — post-Soviet monotony twisting into folklore caricature.

## Genre

First-person folk-horror shooter with improvised weapons and ritual twists.
Compact, replayable, tone-first.

## Platform

The **3dmppc console** — a PSX-like fantasy console. The game ships as a single
`.mppcdisc` cartridge. See [console fit](README.md#console-fit-hard-constraints)
and [`../../platform/specs.md`](../../platform/specs.md). (Originally a Godot 4.5 PC
project; re-targeted to the console.)

## Pillars

1. **Routine as narrative.** The Home → Street → Factory → Home loop *is* the
   story. Repetition with small, meaningful mutations each cycle; small changes
   matter more than spectacle.
2. **Tactile improvised combat.** Bricks, pipes, short-range tools over gun
   fetish. Throws and swings must feel weighty — arc, impact, micro-shake,
   hitstop.
3. **Readable first-person play.** Strong telegraphs, low visual clutter, a
   stable frame. The software rasterizer rewards simple, legible geometry.
4. **Satirical folk horror.** Humor and dread coexist. Post-Soviet mundanity
   caricatured as folklore, backed by unsettling soundscapes. Keep the satire
   empathetic.
5. **Small world, big mood.** Minimal content, focused on tone and pacing — the
   right scope for a first cartridge and for the console's budget.

## Core loop (the day)

1. **Home — Preparation** (30–60s): optional flavour micro-interaction (radio/TV
   flips a mood flag); pick up a brick or pipe; exit to the stairwell.
2. **Street — Commute & Encounter** (3–5 min): traverse 3–5 modular blocks;
   fight two enemy archetypes; scavenge; reach the factory gate.
3. **Factory — Work Under Fire** (2–4 min): survive one escalating encounter
   while assembling a lamppost via a brief ritual/interaction.
4. **Return — Reflection & Mutation**: a day summary; Home gains one new mutation
   (prop, light, picture); loop flags update; the loop resets.

**Win:** complete the factory ritual and return home once.
**Lose:** knocked out anywhere → restart the day.

## Scope (first cartridge)

- **Session length:** 7–12 minutes per loop; restartable anytime.
- **In scope:** one loop, two enemy archetypes, two weapons (brick + pipe), one
  factory ritual, one boss (or a minimal wave escalation), three music moods.
  Buildable content lives in [`../../../mppcdiscs/solid/`](../../../mppcdiscs/solid/).
- **Out of scope (stretch):** progression trees, large inventories, open worlds,
  multiple bosses, firearms. Deferred until the slice ships and feels good.

## Guiding principle

Small, finished, and replayable beats sprawling and unfinished. Ship the loop
first; expand only after it feels good. Every system — from the street layout to
the throwable brick — should reinforce exhaustion, fleeting agency, and the
inevitable return to routine.
