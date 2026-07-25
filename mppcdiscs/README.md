# mppcdiscs/ — the mppc disc library

This is where **games live**. Each subdirectory is one `.mppcdisc` game (a
"disc") in its unpacked, development form: its manifest, sources, and assets.
`mppcburner` compiles a directory from here into a single `.mppcdisc` file, and
the console loads that file at runtime (see
[`../docs/platform/`](../docs/platform/)).

Think of this directory as the **shelf of discs**. The console
(`../src/`) is game-agnostic; anything game-specific lives in a disc under here.
Drop as many discs as you like side by side.

```
mppcdiscs/
  <disc-id>/
    disc.toml       manifest: id, title, ABI version, what to compile/bake/copy
    src/*.cpp       the game — implements rv_de, exports itself with RV_DISC_EXPORT
    assets/         PNGs get baked into texels; everything else is copied in
```

See [`../README.md`](../README.md#authoring-a-game) for the manifest fields and
the burn command, and [`../docs/platform/disc-loading.md`](../docs/platform/disc-loading.md)
for how the packaged disc is loaded.

## Discs here

| Disc                    | What it is                                             |
| ----------------------- | ------------------------------------------------------ |
| [`hello/`](hello/)      | the **example disc** — the smallest complete one, and the thing you copy when starting a real game. Annotated in [`hello/README.md`](hello/README.md). |

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
