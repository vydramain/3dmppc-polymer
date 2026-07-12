# Content — Areas, Loop & Progression

The game is one **day loop** across three areas. Content is deliberately minimal;
the mood and pacing carry it. Fiction for each place is in
[`world.md`](world.md); this doc is the gameplay/structure view.

## The three areas

### Home (30–60s)

- One cramped Khrushchyovka apartment room.
- **Interactable radio/TV** flips a mood flag (also cues the music state).
- Pick up a brick and/or pipe before leaving.
- Bleak courtyard view through the window.
- Exit to the stairwell → Street.
- **Between loops, Home mutates:** one new prop / light / picture per successful
  day. This is the primary visible signal of progression.

### Street (3–5 min)

- A straight, **modular** stretch: 3–5 chunks with clear lanes and collisions.
- A few **alleys** where a Kipuchka can stash a stolen pillar part.
- No cars in the slice.
- Fight the two enemy archetypes (Kipuchka, Midnight Smoker); scavenge spare
  bricks.
- Ends at an unmistakable **checkpoint gate** to the factory (clear signage /
  lighting).
- Spawns are hooked by **chunk index** so difficulty is placed, not random.

### Factory (2–4 min)

- A hangar-like hall: lamp-assembly tables, a welding station, a finishing
  conveyor; a central open floor reserved for the boss.
- The **3-step lamppost ritual** under pressure (see
  [`mechanics.md`](mechanics.md)).
- One escalation: a **boss slice** (Warehouse Manager + Zmey Gorynych, one head
  gimmick cycle) or, for the minimum, a single wave escalation.
- On completion, a **return trigger** sends the player Home.

## The loop

```
        ┌────────────────────────────────────────────┐
        ▼                                            │
  Home ──► Street ──► Factory ──► Return (summary) ──┘
 (prep)   (commute   (ritual +    (Home mutates,
           + fight)   boss)        flags update)
```

- **Win condition:** complete the factory ritual and return home once.
- **Lose condition:** knocked out anywhere → restart the day.
- Each successful loop escalates slightly and mutates Home; repeat toward a
  narrative climax (to be defined).

## Progression & persistence

Progress is measured less by "levels cleared" than by **how strange the day has
become**. Tracked as a small set of **loop flags**:

- day count,
- world-corruption / mood level,
- which Home mutations have been applied,
- pillar/ritual completion state.

These are tiny and fit the memory card (~128 KB) comfortably — no large save
system needed for the slice.

## Content status & next steps

| Area    | Target for the slice                                         |
| ------- | ------------------------------------------------------------ |
| Home    | One room, radio/TV mood toggle, pickups, exit; one mutation slot. |
| Street  | 3–5 greyboxed chunks, one gate, chunk-indexed spawns, prop pass. |
| Factory | Arena blockout, lamppost socket + 3-step ritual, one boss/wave, return trigger. |

Buildable levels, tuning tables, and the save schema live in the disc:
[`../../../mppcdiscs/solid/data/`](../../../mppcdiscs/solid/data/). Keep authored
surreal events and extra chunks in the backlog (see
[`production.md`](production.md)) so the slice stays small.
