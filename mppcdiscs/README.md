# mppcdiscs/ — the mppc disc library

This is where **games live**. Each subdirectory is one `.mppcdisc` game (a
"disc") in its unpacked, development form: its assets, scripts, and data. The
[3dmppc console](../src/) loads a disc from here and reads its assets at runtime;
a disc is later packaged into a single `.mppcdisc` file for distribution (see
[`../docs/platform/`](../docs/platform/)).

Think of this directory as the **shelf of cartridges**. The console
(`src/`) is game-agnostic; anything game-specific lives in a disc under here.
Drop as many discs as you like side by side.

```
mppcdiscs/
  <disc-id>/
    disc.toml       manifest: id, title, entry point, memory budget (TODO — format TBD)
    assets/         art, models, textures, audio — what the console streams in
    scripts/        gameplay logic (planned: Lua — see the console spec)
    data/           levels, tables, tuning, save-schema, loop flags
  solid/            the reference game — see below
```

> **Note:** this is a **stub**. The disc manifest format (`disc.toml`) and the
> exact folder contract are not finalised — they'll be pinned down alongside the
> console's cartridge format in
> [`../docs/platform/`](../docs/platform/) (`cartridge-format.md`,
> `cartridge-abi.md`). For now the layout above is the working convention.

## Discs here

| Disc                    | What it is                                             |
| ----------------------- | ------------------------------------------------------ |
| [`solid/`](solid/)      | *Solidmaid: Alkoldun Vasiliusavich* — the **reference game** used to prove the console. Design docs: [`../docs/mppcdisc/solid/`](../docs/mppcdisc/solid/). |

## Relationship to the rest of the repo

- **`src/`** — the console runtime (game-agnostic). Reads discs from here.
- **`assets/`** (repo root) — the console's own **skeleton demo** assets (the
  rotating cube), *not* a game. Don't confuse it with a disc.
- **`docs/mppcdisc/`** — *design* documentation for discs (one subdir per disc,
  e.g. `solid/`); the actual buildable content lives here in `mppcdiscs/`.
