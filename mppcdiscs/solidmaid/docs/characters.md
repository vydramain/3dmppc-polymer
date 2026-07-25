# Characters — Protagonist, Bestiary & Bosses

## Protagonist — Alkoldun Vasiliusavich

A post-Soviet 1990s factory worker whose mundane routine intersects with faint
occult undertones. ~35, worn by industrial labour, pale, tired, resigned; eyes
permanently hidden behind greasy black hair. Tall, narrow silhouette defined by
a **long matte-brown leather trench coat** over dark-navy factory overalls, a
**tall black wizard hat** with a soft drooping tip, and black combat boots
(*bertsy*). Hands always bare.

The full, fixed visual canon — morphology, outfit, palette, allowed deviations,
forbidden elements, and the T-pose modeling reference — lives in
[`protagonist_profile.md`](protagonist_profile.md). Concept sketch and `.glb`
model are in the disc:
[`../../../mppcdiscs/solid/assets/`](../../../mppcdiscs/solid/assets/). **Treat that
profile as authoritative** for any art or modeling work.

In first person the player mostly sees the world and their hands/held tools;
the full silhouette matters for cutscenes, reflections, the day-summary screen,
and marketing.

## Enemy bestiary

Two archetypes ship in the first disc (the slice); the third is a
post-slice escalation enemy.

- **Kipuchka** *(pickpocket / kikimora)* — **in slice.** Small, fast, jittery
  melee pest. On repeated hits it can briefly stun and **steal the pillar/lamppost
  component**, then flee into an alley; recover it by chasing the Kipuchka down.
  Teaches positioning and punishes greed.
- **Midnight Smokers** *(courtyard phantoms)* — **in slice.** Gopnik-like
  figures made of cigarette smoke: grey tracksuits, dim window-yellow eyes, a
  whispery *"hey, bro…"*. Exhale a **smoke cloud** that cuts visibility and deals
  chip damage. Area-denial pressure; readable pre-warm before the cloud.
- **Leshaki** *(low forest spirits)* — **post-slice.** Thick-necked bruisers in
  crimson jackets with gold chains. High HP, heavy knockback, disorienting.
  Reserved as a later escalation enemy.

Design rules: strong telegraphs (≥ ~300 ms windups), low clutter, loud audio
cues — the software rasterizer and first-person view demand legibility over
detail. See combat in [`mechanics.md`](mechanics.md).

## Bosses (Factory)

Folk-tale figures wearing 90s-institution masks. One is the slice candidate; the
others are future content.

- **Warehouse Manager + Zmey Gorynych** — **slice boss candidate.** A composite
  boss: a corrugated-pipe body with **three heads — Stock, Accounting,
  Write-off.** Arena clutter spawns, audit debuffs, and item burns map to the
  three heads. Beaten by **baiting the heads into harming each other.** (For the
  minimum slice this can degrade to a single escalating wave — see
  [`production.md`](production.md).)
- **Accountant — Baba Yaga** — **future.** Spins in an office chair, curses via
  documents (order / act / certificate) that apply debuffs; can "freeze" the
  screen into tables until a wrong number is found. Vulnerable around the chair
  wheels.
- **Oligarch — Koschei the Deathless** — **future.** Corpulent suit, golden-coin
  eyes (the weak point); heavy ground slams and a grab/split move.

## Casting notes

- The bestiary and bosses are the primary vehicle for the **folklore-as-
  bureaucracy** motif ([`world.md`](world.md)); keep their fiction grounded in
  90s industrial life, with the folk-tale layer showing through behaviour.
- Every enemy must read at 320×240 with paletted textures: silhouette first,
  one signature colour/effect, one telegraphed attack.
