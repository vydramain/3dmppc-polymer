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
channel: Runtime Controls, Output, Project, Files and Code are live panes. The
game's frame inside the Game tile and the terminal are not there yet.

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

### Files

The Files pane shows the project directory as it is on disk, reading a
directory only when it is opened, and follows changes made by any program,
including a save through a temporary file and a rename. New File, New Folder,
Rename and Delete work inside the project only: a new file never replaces an
existing one, a rename never lands on an existing name, and deleting a link
removes the link, not what it points to. Links are shown with `->` and never
entered. The burner's `.mppcburn/` and `.git/` are not shown. A change to
`disc.toml` is read back into the Project pane at once; a running console keeps
the manifest it started with.

### Code

A Code tile is a window of one `nvim --embed` the editor starts with the first
Code tile, using its own config, [`nvim/rv_editor_init.lua`](nvim/rv_editor_init.lua),
not your `init.lua`. nvim must be on `PATH`; without it only the Code tiles say
so. A double click in Files opens the file in the focused Code tile, or in a
new one.

It starts as an ordinary editor: typing inserts, Shift+arrows select, Ctrl+S
saves, Ctrl+Shift+S saves all, Ctrl+Z / Ctrl+Shift+Z undo and redo, Ctrl+C,
Ctrl+X, Ctrl+V use the system clipboard, Ctrl+F searches, Ctrl+H replaces,
Ctrl+G opens nvim's command line. F2 switches to plain Vim and back. Lua, C and
C++ use real tabs 4 wide with a ruler at 128. F5, F6, F7, Shift+F5 and Ctrl+B
stay the editor's own while a Code tile has the keyboard.

A Code tile's status line names the file and marks it `[+]` while unsaved.
Closing a tile whose file is unsaved and shown nowhere else asks Save, Discard
or Cancel; closing the editor asks the same for every unsaved file. A file
changed by another program is re-read when its buffer is clean; when it is not,
nvim asks. Language servers, Problems and search are not connected yet.

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
