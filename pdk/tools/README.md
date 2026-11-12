# pdk/tools — the tools a game author runs

Host programs that turn a prepared directory into something the console can
boot. They run on the **developer's** machine and never on the console; a player
needs neither them nor the compiler they drive.

```sh
cmake -S pdk/tools -B pdk/tools/build -G Ninja && cmake --build pdk/tools/build
```

A separate command from the console's build on purpose: the console is firmware,
and building firmware must not drag in a compiler driver or an image decoder.
These ship as their own package. The development console build
(`-D3DMPPC_DEVTOOLS=ON`) also builds this project, as a project of its own, and
puts the tools next to the console in `build-dev/pconsole/`.

| Tool | Does | Produces |
| --- | --- | --- |
| [`mppcburner`](mppcburner/) | compiles a disc directory, bakes its assets with the bakers and packs it | `.mppcdisc` |
| [`mppctexbaker`](mppctexbaker/) | quantizes and packs a PNG into console texels | `.mppctex` |
| [`mppcmusbaker`](mppcmusbaker/) | converts a 16-bit or 24-bit 44100 Hz WAV into the console's sound | `.pcm` (headerless S16LE mono) |
| [`mppcmodbaker`](mppcmodbaker/) | bakes a glTF 2.0 `.glb` into a console model and its embedded PNG textures | `.mppcmod` + `.png` |
| [`mppcinspector`](mppcinspector/) | checks a disc directory's sources (`disc.toml` and the scenes) before anything is built | findings on stdout, optionally a report file |

`mppcinspector <disc> [--report PATH]` prints each finding as one line,
`file:line: error|warning: text [rule, key ...]`, then the count of errors and
warnings. `--report PATH` also writes the findings to a file (the editor reads
it for its Problems pane). Exit status: 0 no error findings (warnings allowed),
1 error findings, 2 a usage error or a failed run. `mppcburner build` runs it
before it compiles anything and refuses the build on exit status 1; it looks for
it in the `--baker` directory when one is given, else next to itself, then on `$PATH`.
The editor looks for `mppcinspector` in the bakers' directory, the one it passes to the
burner as `--baker`; Build and Run are refused when `mppcinspector` is not there.

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

`mppcburner` also enforces the `[budget]` block of `disc.toml` — the texture
limits and the virtual video-memory size the target machine will hand out. That
budget is the console's own invention rather than anything about the developer's
workstation, which is exactly why it has to be checked deliberately here: the
host would happily pack far more than the console will ever accept.

## Usage

See [`mppctexbaker/README.md`](mppctexbaker/README.md) for the `.mppctex` layout and
the palette traps, and the repository [`README.md`](../README.md) for the whole
authoring flow. Every tool prints `--help`, and all but `mppcinspector` print
`--version`, one line, `<tool> <major>.<minor>`: the PDK version they build for.
