# Production — Milestones, Backlog & Budget

Solo-dev plan for the vertical slice: one loop, two enemies, two weapons, one
ritual, one boss. Small, finished, and replayable beats sprawling and unfinished.

> The original fine-grained task list targeted Godot scenes/scripts. It was **not
> carried forward verbatim** — the game now runs on the mppc console runtime, so
> tasks are re-framed engine-agnostically below. Adjust as the console's
> disc API firms up in [`../../platform/`](../../platform/).

## Cadence & sizing

- **Daily limit:** ~2 hours (one focused session); weekends optional.
- **Weekly capacity:** ~10 hours, keep ~20% buffer for overruns.
- **Sizing rule:** every task fits in 1 session, or it gets split. Anything
  larger becomes a milestone item, not a task.
- **Tracking:** end each session by noting duration, blockers, and the next
  "first click".

## Milestones (slice)

- **M1 — First-person controller & feel.** Gamepad look/move, interact ray +
  prompt, crosshair, brick throw, camera micro-shake + hitstop helper.
- **M2 — Combat pack.** Pipe melee (telegraph + cooldown); two enemies (Kipuchka
  no-steal stub first, then steal; Midnight Smoker AoE with pre-warm); spawner
  (cap 5, min distance, 1.5s grace); damage/HP/death; telegraph + hitstop tuning.
- **M3 — Street blockout & dressing.** Greybox 3–5 chunks with clear lanes and
  collisions; gate/end marker + signage; chunk-indexed spawns; a lighting/mood
  pass; performance pass.
- **M4 — Factory slice.** Arena blockout + spawn points + lamppost socket;
  3-step hold-ritual with progress bar and interrupt-on-hit; boss slice (one
  Zmey head-gimmick cycle) or minimal wave escalation; return trigger + state
  handoff.
- **M5 — Cohesion & polish.** HUD (HP, cooldowns, prompts); wire the three music
  states; Home mutation after a successful loop; QA pass; internal build.

**Stretch (post-slice):** high-fidelity modeling, animation polish, the pillar
ranged unlock, the extra enemy (Leshaki) and bosses (Baba Yaga, Koschei),
additional street chunks and authored surreal events.

## Backlog (atomic tasks, by area)

Legend: `[S]` ≤1 session, `[M]` ≤2 sessions (split if bigger).

**Player & camera**
- [S] First-person controller (move + gamepad look, pitch clamp, sensitivity).
- [S] Interact raycast (2–3m) + on-screen prompt.
- [S] Crosshair + interact tint; subtle head-bob.
- [S] Footstep SFX hook.
- [M] Placeholder FP hand + first-person animation set.

**Weapons**
- [S] Brick: hold/throw, physics arc, cooldown, impact SFX.
- [S] Hitstop helper + camera micro-shake.
- [S] Pipe: short swing, telegraph, cooldown.
- [M] Model/texture brick + pipe; animate swings/impacts.

**Enemies & spawning**
- [S] Kipuchka: chase + melee (steal behaviour added later).
- [S] Midnight Smoker: expanding AoE with pre-warm ring.
- [S] Spawner: cap 5, min distance, 1.5s grace.
- [M] Low-poly enemy meshes + telegraph animations.

**World**
- [S] Home room blockout + radio mood/music toggle.
- [S] Street chunks (3–5) with lanes and one gate.
- [S] Factory arena with lamppost socket and spawn points.
- [M] Prop/model batches for Home → Street → Factory.

**Ritual / assembly**
- [S] 3-step hold-to-assemble with progress bar + interrupt on damage.
- [S] Escalate once at step 2; on complete, spawn return trigger.

**UI / audio**
- [S] HP bar; stamina/cooldown indicators + HUD toggle.
- [S] Music states: home / street / factory (ADPCM, see art-and-audio).
- [S] Crosshair + interact tint overlay.

**Tooling & perf**
- [S] Debug overlay (frame time, active enemies, player HP).
- [M] Cheap lighting/ambient setup for readability under the palette budget.

## Metrics & validation

- Loop time **≤ 12 minutes** for a first-time player.
- Stable frame through spawns, the AoE cloud, and assembly.
- 2 successful loops out of 3 internal attempts.

## Risks & mitigation

| Risk | Mitigation |
| ---- | ---------- |
| 3D scope creep | Lock to one loop, two enemies, two weapons; defer the ranged unlock and extra bosses. |
| Performance dips (software raster) | Simple paletted materials, capped spawns, shared atlases, low poly counts. |
| First-person readability | Strong telegraphs, low visual noise, loud SFX. |
| Gamepad aiming feels bad | Tune sensitivity + light aim-assist; playtest early. |
| Content drain | Reuse modules across Street/Factory; keep props minimal. |
| Motion comfort | Keep head-bob/FOV subtle; expose a comfort toggle. |

## Open questions

1. Pure improvised tools (brick/pipe), or add one simple ranged option beyond the
   pillar unlock?
2. Minimal ritual VFX that reads "occult" without art burden (palette/dither
   tricks)?
3. Which single Home mutation reads best first (poster, light flicker, clutter)?
4. How is progress framed to the player — days survived, rituals completed, or
   world-corruption level?
5. Which surreal events recur vs. stay one-offs to preserve surprise?

## Out of scope (for now)

Distribution/marketing, analytics, localisation, monetisation, the full narrative
arc, and any save system beyond small loop flags on the memory card.
