# Platform — the 3dmppc console

Documentation for the **console** itself: the fantasy hardware, the runtime that
implements it, and the contract a disc must satisfy to run on it.

Everything here is **game-agnostic**. If a fact would still be true when a
completely different disc is inserted, it belongs in this directory. Game
specifics live in that game's own repository, not in this one.

## Contents

| Document                      | What it covers                                          |
| ----------------------------- | ------------------------------------------------------- |
| [`specs.md`](specs.md)        | The target hardware spec — the console's **virtual** budget for display, memory, audio, input and save, what enforces it, and how it maps to the real PSX. |
| [`disc-loading.md`](disc-loading.md) | How a disc is burned into its own `.so`, packed into a single `.mppcdisc`, and loaded at runtime. The burner's four gates, the stored-zip container, the manifest asymmetry, the extract-then-`dlopen` rule, the thin `extern "C"` ABI and its teardown order. Implemented, except the Lua scripting path at the end. |

## Planned documents

These do not exist yet — they're the shape of what "the console" needs to
document as it grows:

- **`runtime.md`** — how the console boots, the main loop, and how a frame is
  produced (rasterizer → framebuffer → SDL3 presenter).

The `.mppcdisc` package layout (formerly the planned `disc-format.md`) and the
console↔disc boundary (formerly `disc-abi.md`) are covered together in
[`disc-loading.md`](disc-loading.md), which documents both as built.

## Boundary with the disc

The console never knows *which* game it's running. It exposes capabilities
(draw geometry, sample textures, read input, read/write the memory card) and
loads a disc that uses them. Keeping that boundary clean is what makes the
"insert a different disc" model real rather than aspirational.
