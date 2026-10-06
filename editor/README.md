# 3dmppc-editor

A separate program for authoring games for the 3dmppc fantasy console. It works
on a game directory, builds it with `mppcburner`, and runs it in a development
console (`3dmppc --dev`) - it links none of their code.

## Features

- Four layouts - Code, Scene, Debug and Burn, chosen once a project is open
- Tiled workspace - open multiple panes; split, close, maximize or turned into
  another kind of pane; dragging a tile's header or a tab to the edge of another
  tile shows where it would land
- Build and run - Ctrl+B to build, F5 to run; runs last successful build after a
  later one failed or was cancelled; builds saved files first when nothing has
  succeeded yet or one changed since
- Reload - F8 reloads Lua scripts and textures without restarting
- Scene editor - layout objects, cameras and volumes in 3D; edit in Wireframe,
  Filled or Textured mode
- Release Candidate - Burn layout builds disc images for final checks before
  release
- Built-in Code editor - Neovim embedded; diagnostics from clangd and
  lua-language-server
- Terminal - shell on a tile, in the project directory

## Limitations

- Scene editor viewport shows `.obj` meshes and volumes but a game only draws
  what its own code draws; the example discs ignore meshes and don't draw quads
  or volumes yet
- Scenes are PDK 0.4 files; a PDK 0.3 console cannot read them

## Requirements

Same as the console: SDL3, LuaJIT, CMake 3.24+, Ninja, Clang with C++23, `git`
and `make`. When building as part of the development kit with
`-D3DMPPC_DEVTOOLS=ON`, the editor is built alongside the console and tools.
See the root README for details and your platform's package list.

## Tools

The editor looks for the console, burner and baker next to its own executable,
where a development build puts all four (`build-dev/pconsole/`). Settings in
File > Settings or `$XDG_CONFIG_HOME/3dmppc-editor/settings.toml` override them:

```toml
[tools]
console = "/path/to/3dmppc"
burner = "/path/to/mppcburner"
baker = "/path/to/mppcbaker"
player = "/path/to/3dmppc"   # built without devtools; there is no default
```

## Quick start

Build the editor on its own:

```sh
cmake -S editor -B editor/build -G Ninja && cmake --build editor/build
./editor/build/3dmppc-editor
```

Or as part of the development kit, next to the development console and the tools:

```sh
cmake -S . -B build-dev -G Ninja -D3DMPPC_DEVTOOLS=ON && cmake --build build-dev
./build-dev/pconsole/3dmppc-editor [PATH]
```

`PATH` is a game directory or its `disc.toml` file; both open the same project.
The editor can also open projects from File > Open Project... in the menu.

## Manual

The complete user manual is built into the editor: Help > Manual (F1). Its
source is in [`texts/rv_editor_manual.md`](texts/rv_editor_manual.md).

## Third-party

The fonts PxPlus IBM VGA 9x16 and EGA 8x14 by VileR, and the Cyrillic of the
5x7 interface font, carry PxPlus's CC BY-SA 4.0 licence. See
[`third_party/pxplus-ibm-vga/ORIGIN.md`](../third_party/pxplus-ibm-vga/ORIGIN.md).
