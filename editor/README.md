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
  each build, and shows each finding in Inspector Report (source `inspector`) with file, line,
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
a tile looks the same in every state. The templates and their states are on the approved page
https://claude.ai/artifact/Bassxq3RAXAVeY1kGRY9VJ and in the Widget Catalog (View > Widget Catalog),
where every template is drawn in each of its states at its minimum and at a roomy size. The widgets
named below are in `src/ui/` unless a path says otherwise.

### Rules

1. A real minimum. A tile cannot be made smaller than the size at which every element in it fits
   whole. The minimum is one size per pane kind, taken over all its states (the longest text
   template, every button, the content area at 3 rows); a state never changes it. There is no More
   menu, no shrinking and no element past an edge.
2. Unique text is whole. Names, hashes, paths, messages and statuses are shown in full, with no
   "...". On the Catppuccin background a long text keeps its length and its area scrolls; in the
   chrome (shelves, transport, status bar, tile headers) the minimum is sized so that the text fits.
3. Only the content scrolls: lists, trees, tables, logs, code, long text. A bar shows only when the
   content really overflows. A shelf, the tab row, the transport, the status bar, a row of buttons
   and a tile header never scroll, and no empty room is kept for a bar. The body of a tile has no
   scrollbar.
4. One content area per pane, not one box per part. The area has at least 3 rows and a
   horizontal bar. A pane with several parts (Session, Release Candidate, Findings) keeps them as
   titled sections inside that one area; a section title may carry its field and buttons on the
   same row. When the area is empty it keeps its background and column headers and says in its first
   row, dim, what is missing; if a filter hid the rows, the row names the filter and has a button
   that resets it. A placeholder (no project, no scene) is such an empty row, not a message.
5. Text in the chrome is a template with a limit on each variable field: a count is at most
   `999+` (a click on it opens the full list), a build or session number is 6 digits, a step name
   is 16 characters (the progress protocol of the tools rejects a longer one,
   `rv_pdklib::rv_progress_step_name_max`). The minimum is taken with every field at its limit.
6. Layout changes only with a size. A state changes text, colour and availability, never a place
   or a size: a button that cannot be used stays and is dimmed, with its reason in a tooltip. A
   message (a question, an error, a result) appears as a strip under the content area, inside the
   tile, with its buttons on its row, and moves nothing above it: the shelf, the column headers and
   the rows stay where they are, and the area gives up only its bottom edge. With nothing to say
   there is no strip and no reserved place. The strip is up to 3 text lines and a longer text
   scrolls inside it; in the logs, Search, Findings, Inspector Report and Hierarchy it is 1 line. The control strips (Runtime and Release
   Controls) are the exception: their status line is a place of 2 text lines that is always there.
   Changing the whole mode of a pane is allowed (project
   closed or open, the Code tile's Unsaved changes, Save As and swap questions, Settings Browse).
7. A click works on the whole drawn area of a button or a row, not on its text.

### Templates and their widgets

| Template | Widget | Widget Catalog section |
| --- | --- | --- |
| Tile frame and tab row: one tab high, one tab is drawn too, `< >` arrows and a `v` list of all tabs when they overflow, the wheel scrolls the row, the active tab is always visible | `rv_editor_tab_strip`, `rv_editor_tab_strip_height` | Tile frame and tab row |
| Shelf: one row, every button always visible, an unavailable one stays with its reason in a tooltip | `rv_editor_shelf_begin` / `rv_editor_shelf_end`, `rv_editor_shelf_fill` | Shelf |
| List, tree, table: Catppuccin background, rows whole and all as wide as the longest, the selection across the full width, status columns pinned to the right edge, a total above | `rv_editor_content_begin` / `rv_editor_content_end`, `rv_editor_content_empty_row`, tables with `pinned_left` / `pinned_right` columns (`rv_editor_widgets_table.hpp`) | List, tree and table |
| Record with a status (Recent Projects, candidates): exactly 2 lines, name and path, and a pinned status column as wide as the longest status word | `rv_editor_record_row` | Records with a status |
| Message strip: drawn only when there is a text, under the content area; text of up to `max_lines` lines (3 by default, 1 in the logs, Search, Findings, Inspector Report and Hierarchy), a longer one scrolls inside, buttons aligned right on the same row. Optional `field` (input in the button row), `lead` (items left of the buttons), `button_states` (a disabled button keeps its place, its reason is the tooltip). The area is given the strip's height at the bottom | `rv_editor_message_strip`, `rv_editor_message_strip_height`, `rv_editor_message_strip_max_height`, `rv_editor_message_strip_min_width` | Message strip |
| Empty row: the first row of an empty list says what is missing; when a filter hid the rows it names the count and the filter and has a button in the row that resets that filter (the function returns true when it is pressed) | `rv_editor_content_empty_row` | List, tree and table |
| Sections in one area: a titled part of the content area, the title row with the section's field and buttons, the rows below; its minimum width counts the controls | `rv_editor_content_section_begin` / `rv_editor_content_section_end`, `rv_editor_content_section_min_width` | Sections in a content area |
| Big ask: replaces the content of the tile (title, text with a bar, a message line, buttons); `body` draws a form or a list in place of the text | `rv_editor_mode_card_begin` / `rv_editor_mode_card_end`, `rv_editor_mode_card_min_size`, `rv_editor_mode_card_spec` | Big ask |
| Form: a label column as wide as the longest label, fields right of it, an error line under the field that can give one, the button row always visible | `rv_editor_form_begin` / `rv_editor_form_row` / `rv_editor_form_custom_begin` / `rv_editor_form_buttons_begin` / `rv_editor_form_end` | Form |
| Transport strip (Runtime and Release Controls): 6 buttons, the Cancel Build or Force Stop slot, labels from 640 px, a fixed height; no note field (it is in Session Notes) | `rv_editor_transport_bar` (`src/panes/rv_editor_pane_controls.hpp`, strip in `src/panes/rv_editor_pane_controls_strip.cpp`), `rv_editor_transport_min_width`, `rv_editor_transport_height` | Transport strip |
| Strip status: under the buttons of a control strip, always 2 text lines high in every state (also with no project, with the same buttons), a template with every field at its limit, wrapped whole | `rv_editor_strip_status`, `rv_editor_strip_status_height`, `rv_editor_strip_status_width_for` (`ui/rv_editor_widgets_strip_status.hpp`) | Transport strip |
| Picture (Game, Candidate Playtest, the Scene viewport): keeps its proportions and takes all the room under the shelf; status and notes lie on a translucent band over it, up to 3 lines | `rv_editor_picture_begin` / `rv_editor_picture_end`, `rv_editor_picture_overlay`, `rv_editor_picture_min_width` | Picture view |
| Facts list: Catppuccin background, whole values, both bars, a key column pinned left, Copy in the row's context menu | `rv_editor_facts_begin` / `rv_editor_facts_row` / `rv_editor_facts_end` | Facts list |
| Status bar: 4 segments, each as wide as its longest template plus an equal share of the spare room; the window's minimum width counts their sum; the Tools segment opens Settings | `rv_editor_status_bar`, `rv_editor_status_bar_min_width` (`ui/rv_editor_widgets_status.hpp`, filled by `src/app/rv_editor_shell_status.cpp`) | Status bar |

A pane puts its parts in a tile with `rv_editor_tiled_area_begin` / `rv_editor_tiled_area_end` and
measures its minimum with `rv_editor_tiled_minimum` and the `rv_editor_tiled_*` helpers of
`src/panes/rv_editor_pane_tiled.hpp`. The minimum holds the strip at its tallest
(`rv_editor_message_strip_max_height`), so a message never changes it. Old widgets (`rv_editor_card`, the ask places and overlays, the reserved message box
`rv_editor_message_box`) are gone; use the templates above.

### Layouts and the Game fit

The four presets (`src/layout/rv_editor_layout_presets.cpp`) follow the approved mockup.

- Code: Files on the left (176 px wide at 1280x720, 240 px at 1920x1080), Code in the middle with
  Console Output, Inspector Report, Terminal and Search Results as tabs under it, the Game on the
  right with Runtime Controls under it.
- Scene: the Scene viewport on top left; under it Assets (tabs Assets, Files, Inspector Report) and
  Inspector side by side, equal in width. On the right, stacked top to bottom: Transform Toolchest
  (one row), Runtime Controls, the Game, Hierarchy (one shelf: buttons, search and order in one
  row) down to the bottom of the window. There is no Console Output in Scene.
- Debug: the Game with Runtime Controls above it on the left, Session (with Observe and Test Case as
  tabs) beside it, the logs across the whole width at the bottom as tabs Runtime Log, Findings,
  Inspector Report and Toolchest.
- Burn: Release Candidate (with Export as a tab) on the left, on the right Release Controls above
  Candidate Playtest, the logs across the whole width at the bottom as tabs Build Log, Runtime Log
  and Inspector Report.
- Inspector Report is a tab beside the logs in every layout and never in a control strip. The log
  shelf stays one row, and the tile minimum is sized for that.
- Assets is a table with the columns Name, Type, Size and Folder; Folder takes the spare width. Type is a square in the colour of the kind and the kind as a word, from the same table as the Files chips (`file_kinds` in `rv_editor_pane_files.cpp`).

Window > Reset Layout applies the preset. When a preset is applied and when the window is resized,
`rv_editor_shell_fit_game` sizes the Game tile in one step (a bisection on a copy of the tree) so that
the picture area under its shelf has exactly the aspect of the project screen (4:3 for 320x240), with
no side bands. The size is the largest one that keeps the other tiles at their minimums, up to a
target picture height from the work area's height `h` (the window less the menu and status bars,
`rv_editor_game_target_h`): `h / 2 - 36` in Code, Debug and Burn and `h / 3 + 16` in Scene. On
1280x720 the picture is 400x300 in Code, Debug and Burn and 320x240 in Scene; on 1920x1080 it is
640x480 and 480x360. In Code the Files tile is `W / 10 + 48` wide (`rv_editor_shell_fit_files`:
176 px at 1280, 240 at 1920). The fit and the Files width apply only to an untouched layout: they
stop once the user has moved a splitter, until Window > Reset Layout.

### Checking a new pane

A pane passes when it fits whole in every state at its minimum and not at one pixel less.

1. Write the minimum as `rv_editor_<pane>_minimum()` in `src/app/rv_editor_shell_minimums.cpp`,
   inside `ui_font_scope` (it then works outside a frame), measured on the longest template of the
   pane over all its states, and return it for the pane kind in that file's switch.
2. In a test include `tests/rv_editor_test_fits_whole.hpp`. `rv_editor_fits_whole(record, tile_rect)`
   returns "" or the first violation: an element outside the tile, a text wider or taller than its
   rectangle, "..." in a visible label, a visible scrollbar on a window not opened with
   `rv_editor_scroll_begin`. Elements inside `rv_editor_scroll_begin` areas are skipped. (The "..."
   search is by substring, so a label such as `Open...` is caught too: write chrome without it.)
3. `rv_editor_check_pane_states(gui, shell, kind, states, min)` draws the pane in a tile of `min`
   for each state, checks `rv_editor_fits_whole` and that the rectangles are the same in every state.
   Call it again with `min` one pixel smaller and expect a violation. The pattern is in
   `tests/rv_editor_test_s13*.cpp` (`_minimums`, `_fits`, `_content`).

The window of such a test is the minimum of the tile or more: tests do not fit the UI to a small
window.

To test a layout in other ways, use the layout probe `tests/rv_editor_test_layout_probe.hpp`.
`rv_editor_probe_draw` draws the body in one frame and records the rectangle of every item and
child window; draw two states at the same size and compare them with `rv_editor_probe_compare`
(empty string means equal). `rv_editor_probe_resize` draws at width w1, w2 and w1 again and
checks that the first and third draws are equal. Tests that draw whole panes use the shared
context of `tests/rv_editor_test_pane_state.hpp` (`tall_gui`); `rv_editor_test_imgui` gives a
display larger than any test window, so nothing is clipped.

## Manual

The complete user manual is built into the editor: Help > Manual (F1). Its
source is in [`texts/rv_editor_manual.md`](texts/rv_editor_manual.md).

## Third-party

The fonts PxPlus IBM VGA 9x16 and EGA 8x14 by VileR, and the Cyrillic of the
5x7 interface font, carry PxPlus's CC BY-SA 4.0 licence. See
[`third_party/pxplus-ibm-vga/ORIGIN.md`](../third_party/pxplus-ibm-vga/ORIGIN.md).
