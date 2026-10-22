# example-lua-bare

The Solid Maid demo as a Lua disc: the same scene, models, sounds and frames as
[`example-cpp-pdkkit`](../example-cpp-pdkkit/README.md), with the logic in Lua scripts instead
of C++. The demo loops every 4320 frames (72 s at 60 frames per second) through
the factory, the street and the flat. A frame's camera, board letters, lamps and
music are pure functions of its number in the period.

The C++ side is one line of disc code: `src/example-lua-bare.cpp` plants the
pdklib script disc (`RV_MPPC_DISC_LUA_DEF`, `pdklib/rv_dscript/rv_dscript.hpp`)
and its entry points (`RV_MPPC_DISC_ENTRY_DEF`, `pdk/include/pdk/de/rv_dv.h`).
Every lifecycle hook is forwarded into the same-named function of the entry
script.

## Where to change what

| Episode | Files | What to change | Apply |
| --- | --- | --- | --- |
| Camera flight: a closed spline through the 20 keys | `scripts/example-lua-bare-layout.lua` (`cam_00`..`cam_19` entries), `scripts/example-lua-bare-math.lua` (`camera_of`, `PERIOD_FRAMES`) | move a `cam_NN` entry to change the path; change the spline or the lens in the math module | Build and Restart for the layout table; Reload Module `example-lua-bare-math` for the per-frame camera. The lamp schedule also calls `camera_of` once in `M.disc_initialize`, so a spline change moves lamp timing only after Build and Restart |
| World meshes: factory, machines, street, house, apartment, furniture | `scripts/example-lua-bare-layout.lua`, `scripts/example-lua-bare.lua` (`add_mesh`, `read_objects`) | the world (16 numbers), model or tint of a mesh entry | Build and Restart |
| Model geometry and pictures: Solid Maid's places, as in example-cpp-pdkkit | `assets/models/*.gltf`, drawn by `draw.put` (`scripts/example-lua-bare-draw.lua`) | replace the .gltf and its .bin in `assets/models/`, and the PNGs it names (mostly in `assets/tex/`); the models are built from Solid Maid's level code, one atlas cell per surface and Solid Maid's tess subdivision; the flat's window has no frame card, since Solid Maid's frame cell has no cut-out texels; the pictures are the PNG files each .gltf names: most are in `assets/tex/`, and `street_16.gltf` names `street_0.png`..`street_3.png` in `assets/models/`; the model build bakes them | Build and Restart |
| Depth order | `scripts/example-lua-bare-draw.lua` (`DECAL_BIAS`, `WORLD_DEPTH_MIN`, `WORLD_DEPTH_MAX`) | the world spans the ordering-table window `pdk.DEPTH_MIN`..`pdk.DEPTH_MAX` minus `DECAL_BIAS` (260); the board's halos and letters are lifted by the bias | Reload Module `example-lua-bare-draw` |
| Board "ОСТАЛОСЬ: 5": glyphs, flicker, halo | `scripts/example-lua-bare-board.lua` (`TEXT`, `FLICKER_TABLE`, `FLICKER_STEP`, `HALO_MODEL`), mesh `board` in the layout table | glyph order, flicker table, step in frames | Reload Module `example-lua-bare-board`; move of the `board` object needs Build and Restart |
| Street lamps: lit on the way out, dark on the way back | `scripts/example-lua-bare-lamps.lua` (`REACH_SQUARED`, `LIGHT_MODEL`), `lamp_1`..`lamp_4` entries in the layout table, `assets/models/lamp_light.gltf` | reach of a lamp, the light model, lamp positions; 4 lamps 8 m apart, alternately on both kerbs | Reload Module `example-lua-bare-lamps` for `LIGHT_MODEL` and the drawing; `REACH_SQUARED` and the schedule run only in `M.disc_initialize`, so a reach change needs Build and Restart; lamp positions come from the layout table: Build and Restart |
| Music per area: 1-s bars through two slots (factory 3 bars, street 5, home 10) | `scripts/example-lua-bare-sound.lua` (`WORKSHOP_MAX_X`, `FLAT_MIN_X`, `BARS`), `assets/snd/*.wav` | area borders on the camera's x (workshop x < 0, flat x > 33.8), bar lists | Reload Module `example-lua-bare-sound` for the constants; track files need Build and Restart |
| Workshop loops and board clack | `scripts/example-lua-bare-sound.lua` (`LOOPS`, `CLACK`), `assets/snd/*.wav` | loop voices and volumes, clack envelope and file | Reload Module `example-lua-bare-sound`; track files need Build and Restart |
| Footsteps per floor | `scripts/example-lua-bare-steps.lua` (`STRIDE`, `ASSETS`, `VOICES`, `VOLUME`), `assets/snd/sfx_step_*.wav` | step stride (0.95 m of the eye's horizontal path), step pairs per floor | Reload Module `example-lua-bare-steps`; sample files need Build and Restart |
| Frame clock and background | `scripts/example-lua-bare.lua` (`frame_update`, `BACKGROUND`) | the clock counts only frames with dt > 0 | Reload (F8) |
| Disc setup and budgets | `disc.toml` | sections and `[budget.*]`, including `script_memory_size` | Build and Restart |
| C++ bridge | `src/example-lua-bare.cpp` | the disc class and entry points | Build and Restart |

The terms in the table (atlas cell, tess, cut-out texels) are explained in
[`example-cpp-pdkkit`'s README](../example-cpp-pdkkit/README.md#where-to-change-what), under the
same table.

Build and Restart is the editor's Restart button (tooltip "Build and Restart").
Ctrl+B builds, F5 runs. The editor's scene check says it plainly: "Scenes are
not reloaded yet. Save, then Build and Restart."

## Reload and Build and Restart

Reload (F8) acts on the file open in the code pane:

- with `scripts/example-lua-bare.lua` open, the button reads "Reload Entry Script"
  and reloads the entry chunk. Its `state` table survives the reload, so the
  frame counter keeps counting;
- with a module open, the button reads "Reload Module: <name>", where `<name>`
  is the file name without `.lua`, the name `require` uses. The module is
  updated in place: every file that requires it sees the new code. A module
  keeps no state of its own;
- a reload that the console refuses leaves the old code running.

The button is on only when all three hold: the editor session is connected, the
disc runs from a directory (live medium), and the console reports
`entry_reloadable` (`rv_editor_app_can_reload`). Otherwise it is off. A reload
that is still in flight also holds it.

The layout table, the sounds, the models and `disc.toml` are read when the disc starts
(`disc_initialize` copies the objects, builds the lamp schedule and the music's
tables), so they need Build and Restart. The state keeps those copies, so a Reload
Module of the layout table does not change the frame. A changed .gltf, with its .bin and
PNGs, needs Build and Restart too, as does the C++ bridge. The drive can refresh a texture
asset in place (the `asset` command, root README), but this demo's pictures are baked by
the model build from the PNG files each .gltf names, so a picture change needs Build and
Restart as well.

## Code layout

- `scripts/example-lua-bare.lua` - the entry chunk. It returns a module table with
  the hooks (`disc_initialize`, `frame_update`, `frame_render`,
  `disc_shutdown`). It takes the objects from the layout table (`scripts/example-lua-bare-layout.lua`),
  copied in `disc_initialize`, and draws in this order: sound, camera, world meshes, lamps, board.
- `scripts/example-lua-bare-math.lua` - the camera loop. Every step is rounded to
  float32 (`f()`) in the operation order of `example-cpp-pdkkit-route.cpp`, which is
  why the frames match the C++ disc bit for bit.
- `scripts/example-lua-bare-draw.lua` - the model put shared by the world, board and lamps,
  with the two depth windows.
- `scripts/example-lua-bare-board.lua`, `scripts/example-lua-bare-lamps.lua` - one episode each.
- `scripts/example-lua-bare-sound.lua` - the music, the loops and the clack; the bars
  of an area play on two voices in turn.
- `scripts/example-lua-bare-steps.lua` - the footsteps, one per 0.95 m of the eye's path.
- `scenes/main.scene.toml` - the scene the layout table was taken from. The disc does
  not read it; `disc.toml` still copies it in.
- `assets/models/*.gltf` - glTF models, each with its `.bin` beside it; `[models]` in
  `disc.toml` names them, and the build bakes them.
- `assets/tex/*.png` - the model pictures each .gltf names, except street_16's four:
  `assets/models/street_0.png`..`street_3.png`, which `street_16.gltf` names beside it.
  The model build bakes them.
- `assets/snd/*.wav` - the music, the loops, the footsteps and the clack, baked to `.pcm`.
- `[textures]` in `disc.toml` lists no files; it keeps only `format = "idx8"`, the
  format the models' textures are baked in.

## Scripts and state

A chunk local or a field of the module table dies with the code on reload.
Only the `state` table survives, so the entry keeps the scene copies, the frame
counter and the music's tables (last frame, area, bar, slots) there. `state` holds
plain numbers, strings, booleans and tables only; a function or a coroutine in it
would keep the old bytecode alive after a reload, and cdata is refused by the
console's state walk, which also caps its nodes. For that reason the read buffer of
the music (`scripts/example-lua-bare-sound.lua`) and the footstep distance table
(`scripts/example-lua-bare-steps.lua`, 4320 numbers) are module locals: a reload drops
them, and the next call rebuilds them.

A failing step returns its code, and the script logs it once (`log_error_once`); the
loop keeps running, and a camera failure skips the picture of that frame. A Lua error
in a call is logged by the console on every failed call. With `--dev` the console
pauses at the first one and sends `event=script_error`, so a reload can fix it.

## Memory

`[budget.pccl]` in `disc.toml` sets `script_memory_size` to 1 MiB (1048576 bytes)
for the whole Lua machine, bootstrap included. Its comment has the measured
range: 358332 to 659645 bytes over 4400 stepped frames with the music, loops and
footsteps. The peak is 659645 bytes.
