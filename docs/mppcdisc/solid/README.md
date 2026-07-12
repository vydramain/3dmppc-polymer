# Solidmaid: Alkoldun Vasiliusavich — the reference cartridge

The main game for the [3dmppc console](../../platform/), shipping as a
`.mppcdisc` package. A first-person folk-horror shooter set in the post-Soviet
1990s: live the exhausting **Home → Street → Factory → Home** day loop, fight
with improvised tools (brick, pipe), perform occult rituals under pressure, and
watch the world mutate a little more each loop.

It is also the project by which we **prove the console**: the first real
cartridge. Once it runs well, the console and the game get split into separate
repositories — so keep this design free of console *implementation* detail
(that belongs in [`../../platform/`](../../platform/)).

The buildable content of this game (assets, scripts, data) lives outside `docs/`,
in [`../../../mppcdiscs/solid/`](../../../mppcdiscs/solid/); this directory holds only
the **design**.

## Status

Migrated from an abandoned standalone **Godot 4.5** prototype (a 2D brawler that
was rebooted into an FPS). The design has been **re-targeted to the mppc console**
(software 3D rasterizer, 320×240, 16-bit, gamepad, memory card). Canonical
direction: the **first-person folk-horror shooter**; the earlier 2D brawler is
history and was not carried forward.

## Documents

| Document                              | Contents                                                  |
| ------------------------------------- | --------------------------------------------------------- |
| [`overview.md`](overview.md)          | Pitch, genre, pillars, the core day loop, scope.          |
| [`world.md`](world.md)                | Setting, tone, and fiction — post-Soviet 90s folk horror. |
| [`characters.md`](characters.md)      | Protagonist, enemy bestiary, and bosses.                  |
| [`mechanics.md`](mechanics.md)        | Controls, combat, the ritual/assembly, feel.              |
| [`content.md`](content.md)            | The three areas, the loop, and progression.               |
| [`art-and-audio.md`](art-and-audio.md)| Visual style, low-poly pipeline, music/SFX — under console budget. |
| [`production.md`](production.md)      | Milestones, backlog, time budget, risks, open questions.  |
| [`protagonist_profile.md`](protagonist_profile.md) | The protagonist's fixed visual canon (modeling reference). |

Buildable content (art, models, scripts, data) lives in the disc:
[`../../../mppcdiscs/solid/`](../../../mppcdiscs/solid/) — e.g. the protagonist `.glb`
model and concept sketch are in
[`../../../mppcdiscs/solid/assets/`](../../../mppcdiscs/solid/assets/).

## Console fit (hard constraints)

From [`../../platform/specs.md`](../../platform/specs.md) — the game must live inside
these; anything that can't is a *console* discussion, not a game one:

- **Display:** 320×240 (or 256×224), 16-bit color + dithering.
- **Textures:** 4-bit / 8-bit paletted, no filtering (nearest).
- **Memory:** 2 MB RAM, 1 MB VRAM.
- **Input:** 1–2 gamepads (no mouse/keyboard).
- **Audio:** sample-based ADPCM, 24 voices, stereo.
- **Save:** memory card (~128 KB) — loop flags fit easily.
