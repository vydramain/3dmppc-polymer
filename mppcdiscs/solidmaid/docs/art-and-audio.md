# Art & Audio

Everything here is bound by the console budget: 320×240 (or 256×224), **16-bit
color + dithering**, **4-bit / 8-bit paletted textures with no filtering**, 1 MB
VRAM, 2 MB RAM, and **sample-based ADPCM audio (24 voices, stereo)**. See
[`../../platform/specs.md`](../../platform/specs.md). Buildable assets live in
[`../../../mppcdiscs/solid/assets/`](../../../mppcdiscs/solid/assets/).

## Visual direction

- **Low-poly, low-fi industrial 3D.** Readable forms, limited palette, flat
  materials. Detail comes from silhouette and palette, not polygon count or
  texture resolution — which is exactly what the software rasterizer wants.
- **Folklore accents** (icons, ornaments, masks) enter sparingly and through
  behaviour, not as pasted-on decoration (see [`world.md`](world.md)).
- **Palette:** strongly limited and practical — pale skin, dark-navy jumpsuit,
  matte-brown coat, black hat/boots/hair. Grounded in 1990s Russian industrial
  texture; no bright accents, no glossy surfaces.
- **Readability first.** At 320×240 with paletted textures, every enemy and prop
  must read by silhouette and one signature colour.

### Protagonist canon

The protagonist's look is **fixed** — morphology, outfit, T-pose, forbidden
elements. Do not reinterpret it here; follow
[`protagonist_profile.md`](protagonist_profile.md). Reference sketch and model:
[`../../../mppcdiscs/solid/assets/`](../../../mppcdiscs/solid/assets/).

## Asset pipeline (low-poly)

Per asset:

1. Block out (or AI-generate) a base mesh under low-poly constraints.
2. Clean up in Blender: decimate if needed, fix normals, simple UV unwrap.
3. **One paletted texture atlas** per asset/batch; albedo only where possible —
   remember textures are 4/8-bit paletted, nearest-sampled.
4. Add simple collision shapes and, for enemies/props, cheap LODs.
5. Import to the console; verify silhouette, scale, and how it dithers at 16-bit.

**Rough budgets** (from the original plan, still valid as effort estimates):
prop batch of 3–4 items ≈ 1 session; enemy base mesh + UV ≈ 1–2 sessions;
environment chunk dressing ≈ 1 session per chunk after blockout.

**VRAM discipline:** with 1 MB VRAM, share palettes and atlases aggressively
across Home → Street → Factory; budget texture memory per area, not per object.

## No shaders — palette & dither instead

The console is a **software rasterizer**: there are no programmable shaders.
Effects that would be shaders elsewhere are done with:

- **Ordered dithering** for gradients and the 16-bit frame.
- **Paletted tricks** — palette swaps/animation for flicker, corruption, mood
  shifts; vertex/per-face colour for lighting instead of pixel lighting.
- **Unlit textured planes / flipbooks** for VFX (smoke clouds, ritual glow)
  rather than particle shaders.
- **Vertex or pose animation** for telegraphs (scale / tint / pose), not
  skeletal shader work.

Keep VFX cheap and legible; the smoke cloud and ritual glow are gameplay
telegraphs first, spectacle second.

## Music

Three mood states swapped by location: **Home / Street / Factory** (plus stingers
for boss/victory/defeat). Original creative intent — post-punk (Home), breakcore
(Street), a heavier boss/Factory theme — carries over as *flavour*, but delivery
is re-targeted to the console:

- Music is **ADPCM samples through the 24-voice SPU**, not streamed audio files;
  favour short loopable stems (~60–120s feel) and sequenced/looped playback
  under the RAM budget.
- Smooth **state transitions** (~300–600 ms crossfade feel) on `home` / `street`
  / `factory` events; keep a consistent perceived loudness.

## SFX

Core set (keep it tight): footsteps (2–3), brick throw + impact (2), pipe swing +
impact (2), enemy grunt/hurt (2), AoE/telegraph pre-warm (1), ritual progress/
complete (1–2), UI click (1). Source by recording foley, synthesis, or CC0
libraries. Punchy, diegetic, loud enough to sell hits alongside hitstop and
camera micro-shake (see [`mechanics.md`](mechanics.md)).

## Animation

Minimal keyframes. First-person hands (idle / walk / hold / throw / melee),
enemy telegraph + attack sets tied to combat timings, and ritual beats. Hook
animation events to the hitstop/shake helpers so feedback lands on frame.

## Sourcing hygiene

Track licences/terms for any external or AI-assisted assets (a `SOURCES` note in
the disc). Keep raw vs. edited audio separate.
