# Platform — the 3dmppc console

Documentation for the **console** itself: the fantasy hardware, the runtime that
implements it, and the contract a cartridge must satisfy to run on it.

Everything here is **game-agnostic**. If a fact would still be true when a
completely different cartridge is inserted, it belongs in this directory. Game
specifics live in [`../mppcdisc/`](../mppcdisc/).

## Contents

| Document                      | What it covers                                          |
| ----------------------------- | ------------------------------------------------------- |
| [`specs.md`](specs.md)        | The target hardware spec (display, memory, audio, input, save) and how it maps to the real PSX. |

## Planned documents

These do not exist yet — they're the shape of what "the console" needs to
document as it grows:

- **`runtime.md`** — how the console boots, the main loop, and how a frame is
  produced (rasterizer → framebuffer → SDL3 presenter).
- **`cartridge-format.md`** — the `.mppcdisc` package layout: what a cartridge
  contains, how it's laid out on disk, and how the console mounts it.
- **`cartridge-abi.md`** — the entry points and services the console exposes to
  a cartridge (the boundary the skeleton cartridge in `src/` currently stands in
  for).

## Boundary with the cartridge

The console never knows *which* game it's running. It exposes capabilities
(draw geometry, sample textures, read input, read/write the memory card) and
loads a cartridge that uses them. Keeping that boundary clean is what makes the
"insert a different cartridge" model real rather than aspirational.
