# 3dmppc — Documentation

This project is two distinct things that must not be confused:

1. **The console** — `3dmppc` itself: a PSX-like fantasy console (the CPU
   rasterizer, runtime, and presentation layer that live in `src/`). It is a
   *machine* that loads and runs cartridges.
2. **The cartridge** — the actual game, which ships as a separate `.mppcdisc`
   package (an *mppc polymer disc*) and is "inserted" into the console.

```
┌─────────────────────────────┐        ┌──────────────────────┐
│  3dmppc  (the console)      │  loads │   game.mppcdisc      │
│  src/ — rasterizer, runtime │◄───────│  the main game       │
│  docs/platform/             │        │  docs/mppcdisc/      │
└─────────────────────────────┘        └──────────────────────┘
```

## ⚠️ The game in this repo is only a skeleton

The demo that currently ships in `src/` (a turntable viewer — it shows the
Solidmaid protagonist model from `mppcdiscs/solid/`, or a textured cube as
fallback) is **not the game**. It is a **skeleton / template cartridge** — the
smallest thing that proves the console boots, uploads geometry, and presents a
frame. It exists to:

- exercise the rasterizer, framebuffer, and SDL3 presenter end to end,
- serve as a reference for how a real cartridge is structured, and
- give us something to run while the console's capabilities grow.

The **real game** is designed separately (see `docs/mppcdisc/`) and will
eventually be built as its own `.mppcdisc` package, loaded by the console rather
than compiled into it. Nothing in `docs/mppcdisc/` describes the demo viewer.

## Where things live

| Directory          | What it documents                                            |
| ------------------ | ------------------------------------------------------------ |
| `docs/platform/`   | The **console**: hardware fantasy spec, runtime, cartridge ABI/format. This is the machine. |
| `docs/mppcdisc/`   | **Disc design docs** — one subdir per game. The reference game (Solidmaid) is [`docs/mppcdisc/solid/`](mppcdisc/solid/). |

Keep the split honest: anything that is true regardless of which game runs
belongs in `docs/platform/`; anything specific to a game belongs under
`docs/mppcdisc/<disc-id>/`.

Note the difference between **docs** and **content**: `docs/mppcdisc/` holds
game *design*, while the buildable games themselves (assets, scripts, data) live
outside `docs/`, in [`../mppcdiscs/`](../mppcdiscs/) — the console's disc library,
from which it loads games at runtime.
