# 3dmppc-editor — the application a game is authored in

A separate program from the console and the tools. It works on a game
directory, drives `mppcburner` to build it and a development console
(`3dmppc --dev`) to run it, and links none of their code.

Today it shows its tiled workspace between a toolbar and a status bar: a tile
can be split, closed with its X box, maximized with its M box or turned into
another kind of pane, and several panes in one tile show as folder tabs. The
layout is saved on exit to `$XDG_CONFIG_HOME/3dmppc-editor/layout`
(`~/.config/...` without the variable), and the Layout menu holds the Code,
Scene, Debug and Build starting layouts of the design references. Only the
Widget Catalog pane has content yet.

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
./editor/build/3dmppc-editor [-s|--scale N]
```

The editor draws one of its pixels per screen pixel, on a HiDPI display too.
`--scale N` (a whole number, 1..8) is the only thing that makes it larger.

Its font is PxPlus IBM VGA 9x16 by VileR (int10h.org), CC BY-SA 4.0: see
[`third_party/pxplus-ibm-vga/`](../third_party/pxplus-ibm-vga/ORIGIN.md).

## Layout

| Path | What |
| --- | --- |
| `src/` | the editor's sources |
| [`docs/3dmppc-editor-v0.4-requirements.md`](docs/3dmppc-editor-v0.4-requirements.md) | requirements and acceptance criteria of the editor MVP |
| [`docs/adr/`](docs/adr/README.md) | architecture decisions: toolkit, tiling, theme, fonts, code editor, Game frame, CMake |
| `docs/references/` | generated design references: a visual direction, not a specification |
