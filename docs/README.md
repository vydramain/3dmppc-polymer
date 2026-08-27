# 3dmppc — Documentation

This project is two distinct things that must not be confused:

1. **The console** — `3dmppc` itself: a PSX-like fantasy console (the CPU
   rasterizer, runtime, and presentation layer that live in `src/`). It is a
   *machine* that loads and runs discs.
2. **The disc** — the actual game, which ships as a separate `.mppcdisc`
   package (an *mppc polymer disc*) and is "inserted" into the console.

```
┌─────────────────────────────┐        ┌──────────────────────┐
│  3dmppc  (the console)      │  loads │   game.mppcdisc      │
│  src/ — rasterizer, runtime │◄───────│  a game, with its    │
│  docs/platform/             │        │  own design docs     │
└─────────────────────────────┘        └──────────────────────┘
```

## ⚠️ The game in this repo is only a skeleton

The disc that currently ships in `src/rv_dmain/` (a "light show" — a cycling
background with a handful of primitives over it) is **not the game**. It is a
**skeleton / template disc** — the smallest thing that proves the console boots,
files primitives, and presents a frame. It exists to:

- exercise the rasterizer, ordering table, and SDL3 presenter end to end,
- serve as a reference for how a real disc is structured, and
- give us something to run while the console's capabilities grow.

A **real game** is developed separately, in its own repository, and is built as
its own `.mppcdisc` package that the console loads rather than compiles in.
Nothing about a game is documented here.

## Where things live

| Directory          | What it documents                                            |
| ------------------ | ------------------------------------------------------------ |
| `docs/platform/`   | The **console**: hardware fantasy spec, runtime, disc ABI/format. This is the machine. |

Keep the split honest: this repository documents the machine, and only the
machine. Anything that is true regardless of which game runs belongs in
`docs/platform/`; anything specific to a game belongs in **that game's own
repository**, next to its assets and code — not here.

Two games in this repo is [`../mppcdiscs/example-cpp/`](../mppcdiscs/example-cpp/)
and [`../mppcdiscs/example-lua/`](../mppcdiscs/example-lua/). There are worked 
**examples** of how a disc is put together, not as a gameanyone is designing.

