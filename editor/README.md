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
- Scene editor - layout objects (id, parent, entity and `[[component]]` data), cameras and
  volumes in 3D; edit in Wireframe, Filled or Textured mode
- Release Candidate - Burn layout builds disc images for final checks before
  release
- Built-in Code editor - Neovim embedded; diagnostics from clangd and
  lua-language-server
- Terminal - shell on a tile, in the project directory
- Inspector - runs `mppcinspector` on the project when it opens, after a save and after
  each build, and shows each finding in Problems (source `inspector`) with file, line,
  level and text

## Limitations

- Scene editor viewport shows `.glb` and `.gltf` meshes and volumes (any other mesh file
  is a placeholder box), but a game only draws what its own code draws, and only a scene
  it reads from its file
- A scene has no version; an object in the removed form 1 (its data as keys such as
  `kind`) is refused, by the editor too

## Requirements

Same as the console: SDL3, LuaJIT, CMake 3.24+, Ninja, Clang with C++23, `git`
and `make`. When building as part of the devtools delivery with
`-D3DMPPC_DEVTOOLS=ON`, the editor is built alongside the console and tools.
See the root README for details and your platform's package list.

## Tools

The editor looks for the console and burner next to its own executable, where a development
build puts them (`build-dev/pconsole/bin/`), and the three bakers in the same directory:
`mppctexbaker` (PNG), `mppcmusbaker` (WAV) and `mppcmodbaker` (.glb), and `mppcinspector`
beside them.
Settings in File > Settings or `$XDG_CONFIG_HOME/3dmppc-editor/settings.toml`
override them. `baker` is the bakers' directory, not an executable. A file path, or a symlink
to one, means the directory where that file really lies, as with `mppcburner --baker`.
Empty means the editor's own directory. The editor checks the three bakers there (present,
executable, version) and reports a path it cannot look up as its own problem; Project > Project
Settings > Toolchain shows them. It passes the directory to `mppcburner` as `--baker` on each
build, unpacked build, release candidate and PNG texture bake, and records their versions in
a release candidate. `mppcinspector` is found in the bakers' directory, where the burner
runs it from, and is checked with the bakers; without it Build and Run are unavailable.
The editor runs `mppcinspector <disc> --report PATH` with the report under the
project's cache directory, `<cache>/<hash>/inspector.tsv` (exit 0 clean or warnings, 1
errors, 2 failed: no findings are shown after 2). The burner runs its own check before
each build too:

```toml
[tools]
console = "/path/to/3dmppc"
burner = "/path/to/mppcburner"
baker = "/path/to/bakers"
player = "/path/to/3dmppc"   # built without devtools; there is no default
```

## Quick start

Build the editor on its own:

```sh
cmake -S editor -B editor/build -G Ninja && cmake --build editor/build
./editor/build/3dmppc-editor
```

Or as part of the devtools delivery, next to the development console and the tools:

```sh
cmake -S . -B build-dev -G Ninja -D3DMPPC_DEVTOOLS=ON && cmake --build build-dev
./build-dev/pconsole/3dmppc-editor [PATH]
```

`PATH` is a game directory or its `disc.toml` file; both open the same project.
The editor can also open projects from File > Open Project... in the menu.

## Tests

The editor has a unit test executable, run by ctest. On its own:

```sh
cmake -S editor -B editor/build -G Ninja -DFETCHCONTENT_SOURCE_DIR_SDL3=<path to an SDL3 source tree>
cmake --build editor/build
ctest --test-dir editor/build --output-on-failure
```

`FETCHCONTENT_SOURCE_DIR_SDL3` names an SDL3 source tree to build when no SDL3 is installed on the
system. Without it, CMake downloads the pinned SDL 3.4.18 archive (`cmake/sdl.cmake`).

In the devtools build the same tests are `ctest --test-dir build-dev/sdk/editor`, but that binary
links against the Steam Runtime and does not start outside it; use the standalone build above for
a quick run. The test binary is not part of the delivery's `bin`.

## Tiled UI rules

Mandatory for every pane and every new pane. The editor is a set of tiles that the user resizes;
a tile must look the same in every state. The widgets named below are in `src/ui/`.

1. Click area. Anything that looks like a button, card or row reacts to a click on its whole
   drawn area, not on its text. Use `rv_editor_card`, `rv_editor_table_row_select` and
   `rv_editor_button`; do not put an interactive item inside a card (it would take the clicks).
2. Text. Long variable text (a status, a message, a reason) wraps in its area and is read in
   full: `rv_editor_status` and `rv_editor_label` wrap by default, `rv_editor_message_line`
   wraps within the lines it reserves. A short label or a table cell stays on one line and, when
   it does not fit, ends with "..." and shows the whole text in a tooltip:
   `rv_editor_table_cell_text`, `rv_editor_ellipsize`.
3. Paths. A path that does not fit shows its end with a leading "..." and the full path in a
   tooltip (`rv_editor_path_tail`, `rv_editor_path_row`). A path field
   (`rv_editor_path_field`) shows the tail until it is focused, relative to the project root when
   the path is inside it.
4. Nothing appears, disappears, moves or resizes by state. Every block, button and message line
   has its place from the start: a button that cannot be used is disabled with its reason in a
   tooltip; a line with nothing to say is an empty reserved place (`rv_editor_message_line`,
   `rv_editor_ask_place_begin` / `rv_editor_ask_place_end` for an ask); a button whose label
   changes keeps one width (`rv_editor_button_fixed`); a pane with an action that is not always
   needed keeps its slot. Layout changes only when the window or a tile is resized:
   `rv_editor_flow` and the transport bar depend on the width only, and the minimum of a tile is
   constant per pane kind. Switching the whole mode of a pane is allowed (project closed or
   open, the Code tile's Unsaved changes or Save As ask and its swap recovery, Settings Browse).

Allowed: an error that fills a reserved line; a Build button that is dimmed while a build runs
and says why; a row of buttons that wraps when the tile is narrower. Forbidden: an error line
that pushes the content down when it shows; a button that is added only when a file is open; a
label that gets wider when its text changes; a path cut at its end.

To test a pane, use the layout probe `tests/rv_editor_test_layout_probe.hpp`.
`rv_editor_probe_draw` draws the body in one frame and records the rectangle of every item and
child window; draw two states at the same size and compare them with `rv_editor_probe_compare`
(empty string means equal). `rv_editor_probe_resize` draws at width w1, w2 and w1 again and
checks that the first and third draws are equal. Tests that draw whole panes use the shared
context of `tests/rv_editor_test_pane_state.hpp` (`tall_gui`, a display tall enough for a whole
pane); `rv_editor_test_imgui` gives a display larger than any test window, so nothing is clipped.

## Manual

The complete user manual is built into the editor: Help > Manual (F1). Its
source is in [`texts/rv_editor_manual.md`](texts/rv_editor_manual.md).

## Third-party

The fonts PxPlus IBM VGA 9x16 and EGA 8x14 by VileR, and the Cyrillic of the
5x7 interface font, carry PxPlus's CC BY-SA 4.0 licence. See
[`third_party/pxplus-ibm-vga/ORIGIN.md`](../third_party/pxplus-ibm-vga/ORIGIN.md).
