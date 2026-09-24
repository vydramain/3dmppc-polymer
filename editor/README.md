# 3dmppc-editor — the application a game is authored in

A separate program from the console and the tools. It works on a game
directory, drives `mppcburner` to build it and a development console
(`3dmppc --dev`) to run it, and links none of their code.

Today it shows its tiled workspace between the menu bar and a status bar: a tile
can be split, closed with its X box, maximized with its M box or turned into
another kind of pane, and several panes in one tile show as folder tabs. The
layout is saved on exit to `$XDG_CONFIG_HOME/3dmppc-editor/layout`
(`~/.config/...` without the variable), and the Layout menu holds the Code,
Scene, Debug and Build starting layouts of the design references.

It opens a game directory, builds it with `mppcburner` and runs the result in
a development console with its own window, driven over the console's dev
channel: Runtime Controls, Output and Project are live panes. The game's frame
inside the Game tile, the code editor, the file tree and the terminal are not
there yet.

## Building

On its own:

```sh
cmake -S editor -B editor/build -G Ninja && cmake --build editor/build
./editor/build/3dmppc-editor
```

As part of the development kit, next to the development console and the tools:

```sh
cmake -S . -B build-dev -G Ninja -D3DMPPC_DEVTOOLS=ON && cmake --build build-dev
./build-dev/pconsole/3dmppc-editor
```

A player build of the console never builds it.

## Running

```sh
./editor/build/3dmppc-editor [-s|--scale N] [PATH]
```

`PATH` is a game directory or its `disc.toml`; both open the same project.
File > Open Folder and File > Open disc.toml do the same from the menu.

The editor draws one of its pixels per screen pixel, on a HiDPI display too.
`--scale N` (a whole number, 1..8) is the only thing that makes it larger.

### Building and running a game

| Command | Key | What it does |
| --- | --- | --- |
| Build | Ctrl+B | `mppcburner build <root> --unpacked <cache>/builds/<n> --baker <mppcbaker>` in the background |
| Run / Resume | F5 | `3dmppc --dev --memcard <state>/memcard.mppccard <cache>/builds/<n>` for the last build, which must have succeeded; on a paused game, `resume` |
| Pause | F6 | `pause`, shown as Pausing until the console confirms it |
| Step Frame | F7 | `step`: one frame of a paused game |
| Stop | Shift+F5 | `quit`, then waits; Force Stop kills a console that does not end |
| Run Last Successful Build | | runs the older build after a later one failed or was cancelled |

Each build goes to a new numbered directory and counts only when the burner
exits 0; a failed or cancelled build is deleted and never runs. A running game
keeps running while the next build is made. Output shows the editor's, the
build's and the runtime's lines; the dev channel's own lines are there too,
off by default. Closing the editor stops the build and the game it started.

The editor speaks dev protocol 1 and refuses any other console with the
reason, a player build of the console included.

### Where things go

| What | Where |
| --- | --- |
| Layout | `$XDG_CONFIG_HOME/3dmppc-editor/layout` |
| Settings | `$XDG_CONFIG_HOME/3dmppc-editor/settings.toml` |
| Builds | `$XDG_CACHE_HOME/3dmppc-editor/<hash of the project path>/builds/<n>` |
| Memory card | `$XDG_STATE_HOME/3dmppc-editor/<hash of the project path>/memcard.mppccard` |

Without the variables: `~/.config`, `~/.cache`, `~/.local/state`. Nothing is
written into the game directory except what `mppcburner` itself leaves there
(`.mppcburn/`).

The console, the burner and the baker are looked for next to the editor's own
executable, which is where a development build puts all four
(`build-dev/pconsole/`). `settings.toml` overrides any of them, in the same
dialect as `disc.toml`:

```toml
[tools]
console = "/path/to/3dmppc"
burner = "/path/to/mppcburner"
baker = "/path/to/mppcbaker"
```

A missing tool disables only the commands that need it, with the reason; the
Project pane shows each path and the version the tool reports
(`mppcburner --version`).

### Font

Its font is PxPlus IBM VGA 9x16 by VileR (int10h.org), CC BY-SA 4.0: see
[`third_party/pxplus-ibm-vga/`](../third_party/pxplus-ibm-vga/ORIGIN.md).

## Layout

| Path | What |
| --- | --- |
| `src/` | the editor's sources |
| [`docs/3dmppc-editor-v0.4-requirements.md`](docs/3dmppc-editor-v0.4-requirements.md) | requirements and acceptance criteria of the editor MVP |
| [`docs/adr/`](docs/adr/README.md) | architecture decisions: toolkit, tiling, theme, fonts, code editor, Game frame, CMake |
| [`docs/sgi-irix-ux.md`](docs/sgi-irix-ux.md) | research: how SGI IRIX technical applications looked and behaved, with sources |
| `docs/references/` | generated design references: a visual direction, not a specification |
