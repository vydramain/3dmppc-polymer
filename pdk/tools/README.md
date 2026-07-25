# pdk/tools — the tools a game author runs

Host programs that turn a prepared directory into something the console can
boot. They run on the **developer's** machine and never on the console; a player
needs neither them nor the compiler they drive.

```sh
cmake -S pdk/tools -B pdk/tools/build -G Ninja && cmake --build pdk/tools/build
```

A separate command from the console's build on purpose: the console is firmware,
and building firmware must not drag in a compiler driver or an image decoder.
These ship as their own package.

| Tool | Does | Produces |
| --- | --- | --- |
| [`mppcburner`](mppcburner/) | compiles a disc directory and packs it | `.mppcdisc` |
| [`mppcbaker`](mppcbaker/) | quantizes and packs an image into console texels | `.mppctex` |

Tools are named for the **act**, artifacts for the **thing**. `mppcdisc` is a
disc, so no tool may be called that — otherwise every sentence in the
documentation becomes ambiguous.

## Where they sit

```
pdk/        the contract     — what the console implements     ─┐ compiled
pdklib/     the library      — helpers built on the contract   ─┘ into a disc
pdk/tools/   the tools        — binaries the author runs          ← you are here
src/        the console      — implements pdk/
mppcdiscs/  the games
```

The tools include `pdk/` headers (they enforce the console's texel formats and
stamp the ABI version) but link nothing from `src/`.

## Usage

See [`mppcbaker/README.md`](mppcbaker/README.md) for the `.mppctex` layout and
the palette traps, and the repository [`README.md`](../README.md) for the whole
authoring flow. Both tools print `--help`.
