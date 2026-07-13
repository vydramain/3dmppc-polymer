# Platform — the 3dmppc console

Documentation for the **console** itself: the fantasy hardware, the runtime that
implements it, and the contract a disc must satisfy to run on it.

Everything here is **game-agnostic**. If a fact would still be true when a
completely different disc is inserted, it belongs in this directory. Game
specifics live in [`../mppcdisc/`](../mppcdisc/).

## Contents

| Document                      | What it covers                                          |
| ----------------------------- | ------------------------------------------------------- |
| [`specs.md`](specs.md)        | The target hardware spec (display, memory, audio, input, save) and how it maps to the real PSX. |
| [`disc-loading.md`](disc-loading.md) | How a disc is built into its own `.so`, packaged into a single `.mppcdisc`, and loaded at runtime (`3dmppc solid.mppcdisc`). Covers the thin `extern "C"` ABI and the future Lua scripting path. Design doc — not yet implemented. |

## Planned documents

These do not exist yet — they're the shape of what "the console" needs to
document as it grows:

- **`runtime.md`** — how the console boots, the main loop, and how a frame is
  produced (rasterizer → framebuffer → SDL3 presenter).

The `.mppcdisc` package layout (formerly the planned `disc-format.md`) and the
console↔disc boundary (formerly `disc-abi.md`) are now covered together in
[`disc-loading.md`](disc-loading.md) as a single design doc.

## Boundary with the disc

The console never knows *which* game it's running. It exposes capabilities
(draw geometry, sample textures, read input, read/write the memory card) and
loads a disc that uses them. Keeping that boundary clean is what makes the
"insert a different disc" model real rather than aspirational.
