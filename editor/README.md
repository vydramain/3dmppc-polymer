# 3dmppc-editor — the application a game is authored in

A separate program from the console and the tools. It works on a game
directory, drives `mppcburner` to build it and a development console
(`3dmppc --dev`) to run it, and links none of their code.

Today it shows its tiled workspace between the menu bar and a status bar: a tile
can be split, closed with its X box, maximized with its M box or turned into
another kind of pane, and several panes in one tile show as folder tabs. The
tile with the focus wears a brass frame.

Three workspaces share the same panes, documents, build and runtime: Default,
Test and Release, switched at the right end of the menu bar or in the Layout
menu. Each keeps its own tiles; switching changes only what is shown, never a
process, a build, a buffer or the log. Each is saved on exit to its own file,
`$XDG_CONFIG_HOME/3dmppc-editor/layout`, `layout-test` and `layout-release`
(`~/.config/...` without the variable), and the one in front is remembered.
Without a saved file a workspace starts from its preset; Layout > Reset resets
only the workspace in front. Default is Files on the left, Code over Output and
Terminal (two tabs) in the middle, and Game over Runtime Controls on the right;
a layout saved by an earlier editor stays Default's. Layout > Reference Layouts
puts the Code or Scene layout of the design references, whose Scene and other
panes are not written yet, into the workspace in front.

It opens a game directory, builds it with `mppcburner` and runs the result in
a development console that draws into the Game tile, driven over the console's
dev channel: Game, Runtime Controls, Output, Terminal, Files and Code are live
panes, and Window > Project Settings shows the paths and versions of the project
and the tools.

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
| Run / Resume | F5 | `3dmppc --dev --frame-fd 3 --memcard <state>/memcard.mppccard <cache>/builds/<n>` for the last build, which must have succeeded; on a paused game, `resume` |
| Pause | F6 | `pause`, shown as Pausing until the console confirms it |
| Step Frame | F7 | `step`: one frame of a paused game |
| Stop | Shift+F5 | `quit`, then waits; Force Stop kills a console that does not end |
| Run Last Successful Build | | runs the older build after a later one failed or was cancelled |

Each build goes to a new numbered directory and counts only when the burner
exits 0; a failed or cancelled build is deleted and never runs. A running game
keeps running while the next build is made. Output shows the editor's, the
build's and the runtime's lines, the Runtime Log and the Build Log one source
each until more are ticked; the dev channel's own lines are there too,
off by default, without the frame events the console sends sixty times a
second. A console that stops reading its input gets at most 1 MiB of queued
commands; past that a command is refused and Output says so once, and Stop
then offers Force Stop. Closing the editor stops the build and the game it
started.

The editor speaks dev protocol 2 and refuses any other console with the
reason, a player build of the console included.

### Test

For playing the game and finding out what it does. A one-row strip of Runtime
Controls (Build, Run/Resume, Pause, Step, Stop, and Reload while the running
disc can take one) sits over the Game, with Observe, Code and Files as tabs
beside it and the Runtime Log, Findings and a Terminal underneath.

- **Observe** shows what the console says about itself in its status: session,
  frame, disc, code hash, PDK, the entry script's revision and whether a reload
  changed it, Lua memory. On a disc with a Lua machine it lists the persistent
  state with `keys` and reads pinned values with `get`, read-only. Each value
  says when it is true: `frame N` for a read on a paused machine (the console
  answers in order, so nothing ran in between), `sampled <time>, running`
  otherwise; the reads are never one snapshot. A paused machine is read again
  after every Step. A disc without a Lua machine says there is no state to
  inspect.
- **Reload** (F8, Run > Reload Entry Script) sends `reload entry`; it is offered
  only for a disc running from a directory with an entry script, and the answer
  or the refusal is in the Runtime Log.
- **Findings**: Capture Frame writes the frame the Game shows as a PNG; Record
  Finding writes a title, steps, expected and actual behaviour with the session,
  frame, build, runtime, disc, code hash and entry revision, and the log since
  the session started.

### Release

For checking one disc image before it is released. Release Controls (Build
Candidate, Run Candidate, Stop, Export Report) sit over the Release Candidate
beside the Candidate Playtest, a Game tile, with the Build Log and the Playtest
Log underneath.

- **Build Candidate** runs `mppcburner build <root> -o <cache>/candidates/<n>.mppcdisc
  --baker <mppcbaker>`: a new number each time, never written over. A failed
  build makes no candidate and says so; the older ones stay.
- A candidate shows its image, SHA-256, size, build time, command and tool
  versions, and whether a project file changed since its build started.
- **Run Candidate** runs that image on the development console with a memory
  card of its own, `<n>.mppccard`; the unpacked development build never stands
  in for it.
- Seven checks, all required: Build and Disc loads are set by the editor
  (the burner exited 0; the console mounted the image and answered its
  status), Launch, Input, Audio, Main scenario and Exit by the operator after
  playing it. Each is Not run, Running, Passed, Failed, Blocked or Skipped, and
  Skipped does not count. Exit cannot pass after a run that was killed,
  crashed or force-stopped. They cover only what each says, not the whole game.
- Results belong to the image's SHA-256. Verify Bytes, Run Candidate and Approve
  read the image again; different bytes make every result and the decision void.
- Build succeeded, the checks passed and the decision are three separate
  lines. Approve needs every check passed on the verified bytes; Reject and
  Undecide are always there.
- **Export Report** writes `<cache>/candidates/<n>-report-<time>.txt` with the
  candidate, the checks, the decision and the build and playtest logs. It sends
  nothing anywhere.

Candidates and their checks live in the window's memory: the images stay on
disk, their checks go when the editor closes.

### Game

The Game tile shows the console's own frame. The editor makes a shared memory
object and hands it to the console as `--frame-fd`; the console writes each
finished frame there instead of opening a window, and the tile draws it without
smoothing at the size its Fit, Integer, 1x, 2x and 3x buttons (or View > Game
Scale) choose: Fit, the default, is the largest size that keeps the frame's
proportions, fractional scales included; Integer the largest whole multiple;
a fixed multiple that does not fit is lowered to one that does, and the status
line says so. What the frame leaves over is dark. A click on the picture gives
the game the keyboard,
with the console's own keys (arrows, Space or Z, X, C, V, Q, E, Tab, Esc);
Shift+Esc, a click elsewhere, closing or hiding the Game tile, another window
taking the keyboard, opening another project and starting a new console take it
back, and the game gets every key up at once. Paused, the tile shows the
console's pause picture. Sound still comes from the console.

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
so. A double click in Files opens the file in the focused Code tile, else in
the Code tile used last, else in a new one.

A Code tile keeps the files it has shown as tabs above the text: a click on a
tab shows that file again, opening a file that already has a tab brings the tab
forward, and a right click offers Close Tab (the file stays loaded; an unsaved
one keeps its tab). Split Right or Split Down on a Code tile makes a second Code
tile, another nvim window on the same file.

It starts as an ordinary editor: typing inserts, Shift+arrows select, Ctrl+S
saves, Ctrl+Shift+S saves all, Ctrl+Z / Ctrl+Shift+Z undo and redo, Ctrl+C,
Ctrl+X, Ctrl+V use the system clipboard, Ctrl+F searches, Ctrl+H replaces,
Ctrl+G opens nvim's command line. F2 switches to plain Vim and back. C and
C++ indent with 4 spaces and a ruler at column 129, everything else with 2
spaces; the wheel scrolls, and the cursor takes nvim's shape for each mode. F5, F6, F7, Shift+F5 and Ctrl+B
stay the editor's own while a Code tile has the keyboard.

A Code tile's header and status line name the file and mark it `[+]` while
unsaved; a new one is Untitled. A click puts the cursor where it lands, a drag
selects. Closing a tile whose file is unsaved and shown nowhere else asks Save,
Discard or Cancel; closing the editor or opening another project asks Save All,
Discard All or Cancel for every unsaved file. A file counts as saved only when
nvim reports the write: one it could not write stays unsaved and open, listed
with nvim's reason, and nothing closes until each is saved or the user says
Discard. An Untitled file gets a name through Save As, here or in File, which
never replaces an existing file. A file changed by another program is re-read
when its buffer is clean; when it is not, nvim asks. Language servers, Problems
and search are not connected yet.

### Terminal

A Terminal tile runs your `$SHELL` (else `/bin/sh`) on a pseudo-terminal of its
own, in the project's directory, with `TERM=xterm-256color`. The screen is kept
by [libvterm](../third_party/libvterm/ORIGIN.md) and drawn in the code font on
Catppuccin Mocha. Ctrl+C interrupts, the terminal takes the tile's size, the
wheel scrolls back through up to 5000 lines and typing returns to the bottom.
F5, F6, F7 and Ctrl+B stay the editor's own. It is a session of its own that
never sees the console's dev channel. When the shell ends the tile says how and
offers Start Again; closing the tile or the editor hangs up the shell and ends
everything started in it, background jobs included. Every Terminal tile is its
own shell.

### Where things go

| What | Where |
| --- | --- |
| Layouts | `$XDG_CONFIG_HOME/3dmppc-editor/layout` (Default), `layout-test`, `layout-release` |
| Settings | `$XDG_CONFIG_HOME/3dmppc-editor/settings.toml` |
| Code text size, Game scale, workspace | `$XDG_CONFIG_HOME/3dmppc-editor/view` |
| Builds | `$XDG_CACHE_HOME/3dmppc-editor/<hash of the project path>/builds/<n>` |
| Memory card | `$XDG_STATE_HOME/3dmppc-editor/<hash of the project path>/memcard.mppccard` |
| Findings | `$XDG_STATE_HOME/3dmppc-editor/<hash of the project path>/findings/` |
| Release candidates, their memory cards and reports | `$XDG_CACHE_HOME/3dmppc-editor/<hash of the project path>/candidates/` |

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

### Fonts

The interface draws in pdklib's 5x7 bitmap font (`rv_font`, 8 px high, one
blank column between letters) with no magnification beyond `--scale`. Code and
Output draw in PxPlus IBM VGA 9x16 by VileR (int10h.org): View > Code Text Size
picks Normal (16 px, the default), Large (32 px) or Small (PxPlus IBM EGA 8x14
at 14 px), each times `--scale`, and the choice is kept. The two font files and
the Cyrillic of the 5x7 font carry
PxPlus's CC BY-SA 4.0 licence: see
[`third_party/pxplus-ibm-vga/`](../third_party/pxplus-ibm-vga/ORIGIN.md).

## Layout

| Path | What |
| --- | --- |
| `src/` | the editor's sources |
| [`docs/3dmppc-editor-v0.4-requirements.md`](docs/3dmppc-editor-v0.4-requirements.md) | requirements and acceptance criteria of the editor MVP |
| [`docs/adr/`](docs/adr/README.md) | architecture decisions: toolkit, tiling, theme, fonts, code editor, Game frame, CMake |
| [`docs/icons.md`](docs/icons.md) | every place the editor needs an icon, and the picture or coloured letter it has now |
| [`docs/sgi-irix-ux.md`](docs/sgi-irix-ux.md) | research: how SGI IRIX technical applications looked and behaved, with sources |
| `docs/references/` | generated design references: a visual direction, not a specification |
