# mppcdiscs/ — the mppc disc library

This is where **games live**. Each subdirectory is one `.mppcdisc` game (a
"disc") in its unpacked, development form: its manifest, sources, and assets.
`mppcburner` compiles a directory from here into a single `.mppcdisc` file, and
the console loads that file at runtime
(see [`../src/rv_pconsole/rv_pcloader.hpp`](../src/rv_pconsole/rv_pcloader.hpp)).

Think of this directory as the **shelf of discs**. The console
(`../src/`) is game-agnostic; anything game-specific lives in a disc under here.
Drop as many discs as you like side by side.

```
mppcdiscs/
  <disc-id>/
    disc.toml       manifest: id, title, what to compile/bake/copy
    src/*.cpp       the game — implements rv_de, exports itself with RV_MPPC_DISC_ENTRY_DEF
    assets/         PNGs get baked into texels; everything else is copied in
```

See [`../README.md`](../README.md#authoring-a-game) for the manifest fields and
the burn command, and [`../src/rv_pconsole/rv_pcloader.hpp`](../src/rv_pconsole/rv_pcloader.hpp)
for how the packaged disc is loaded.

## Discs here

| Disc                             | What it is                                             |
| -------------------------------- | ------------------------------------------------------ |
| [`example-cpp/`](example-cpp/)   | the **example disc** — the smallest complete one, and the thing you copy when starting a real game. |
| [`example-lua/`](example-lua/)   | the same shape plus `scripts/`: all four lifecycle hooks (`disc_initialize`, `frame_update`, `frame_render`, `disc_shutdown`) forward one-to-one into a Lua chunk through `rv_cl`, and the script reaches `rv_cv`/`rv_ca`/`rv_cio` through the same exported console functions a C++ disc calls — no wrapper layer. |

## Real games live in their own repositories

This repository is the **console** plus that one example. A game is not a
subdirectory of the machine that runs it: it gets its own repository, carrying
its own assets, code, and design docs, and it is burned against this console's
`pdk/` contract. Nothing here should ever need to name a specific game — if it
does, that is a bug in the layering, not a missing folder.

To work on such a game against a local console checkout, symlink it onto the
shelf; the root [`.gitignore`](../.gitignore) keeps those symlinks out of this
repository.

## Relationship to the rest of the repo

- **`../src/`** — the console runtime (game-agnostic). Loads discs; never names
  one.
- **`../src/rv_dmain/`** — the built-in **service test**. It is a disc, but a
  linked-in one: a diagnostics screen, not a game.
- **`../pdk/`, `../pdklib/`** — the contract a disc is written against, and the
  disc-side conveniences built on it. A disc links these and nothing else.
