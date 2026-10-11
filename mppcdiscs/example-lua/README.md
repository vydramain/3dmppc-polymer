# example-lua

The Solid Maid demo as a Lua disc: the same scene, models, sounds and frames as
[`example-cpp`](../example-cpp/README.md), with the logic in Lua scripts instead
of C++. The demo loops every 1080 frames (18 s at 60 frames per second) through
the factory, the street and the flat. A frame's camera, board letters, lamps and
music are pure functions of its number in the period.

The C++ side is one line of disc code: `src/example-lua.cpp` plants the
pdklib script disc (`RV_MPPC_DISC_LUA_DEF`, `pdklib/rv_dscript/rv_dscript.hpp`)
and its entry points (`RV_MPPC_DISC_ENTRY_DEF`, `pdk/include/pdk/de/rv_dv.h`).
Every lifecycle hook is forwarded into the same-named function of the entry
script.

## Where to change what

| Episode | Files | What to change | Apply |
| --- | --- | --- | --- |
| Camera flight: a closed spline through the 18 keys | `scenes/main.scene.toml` (`cam_00`..`cam_17`), `scripts/example-lua-math.lua` (`camera_of`, `PERIOD_FRAMES`) | move a `cam_NN` object to change the path; change the spline or the lens in the math module | Build and Restart for the scene; Reload Module `example-lua-math` for the per-frame camera. The lamp schedule also calls `camera_of` once in `M.disc_initialize`, so a spline change moves lamp timing only after Build and Restart |
| World meshes: factory, street, house, apartment | `scenes/main.scene.toml`, `scripts/example-lua.lua` (`add_mesh`, `read_objects`) | position, rotation or scale of a mesh object | Build and Restart |
| Model geometry and pictures | `models/*.glb` | replace the .glb in `models/` | Build and Restart |
| Board "ОСТАЛОСЬ: 5": glyphs, flicker, halo | `scripts/example-lua-board.lua` (`TEXT`, `FLICKER_TABLE`, `FLICKER_STEP`, `HALO_MODEL`), mesh `board` in the scene | glyph order, flicker table, step in frames | Reload Module `example-lua-board`; move of the `board` object needs Build and Restart |
| Street lamps: lit on the way out, dark on the way back | `scripts/example-lua-lamps.lua` (`REACH_SQUARED`, `LIGHT_MODEL`), `lamp_N` meshes in the scene, `models/lamp_light.glb` | reach of a lamp, the light model, lamp positions | Reload Module `example-lua-lamps` for `LIGHT_MODEL` and the drawing; `REACH_SQUARED` and the schedule run only in `M.disc_initialize`, so a reach or threshold change needs Build and Restart; lamp positions come from the scene: Build and Restart |
| Music per area and board clack | `scripts/example-lua-sound.lua` (`WORKSHOP_MAX_X`, `FLAT_MIN_X`, `MUSIC`, `CLACK`), `assets/snd/*.wav` | area borders on the camera's x, track files | Reload Module `example-lua-sound` for the constants; track files need Build and Restart |
| Frame clock and background | `scripts/example-lua.lua` (`frame_update`, `BACKGROUND`) | the clock counts only frames with dt > 0 | Reload (F8) |
| Disc setup and budgets | `disc.toml` | sections and `[budget.*]`, including `script_memory_size` | Build and Restart |
| C++ bridge | `src/example-lua.cpp` | the disc class and entry points | Build and Restart |

Build and Restart is the editor's Restart button (tooltip "Build and Restart").
Ctrl+B builds, F5 runs. The editor's scene check says it plainly: "Scenes are
not reloaded yet. Save, then Build and Restart."

## Reload and Build and Restart

Reload (F8) acts on the file open in the code pane:

- with `scripts/example-lua.lua` open, the button reads "Reload Entry Script"
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

The scene, the sounds, the models and `disc.toml` are read when the disc
starts (`disc_initialize` reads the scene and builds the lamp schedule), so
they need Build and Restart. The drive refreshes textures in place (the `asset`
command), but not model geometry: a changed .glb needs Build and Restart. The
same holds for the C++ bridge.

## Code layout

- `scripts/example-lua.lua` - the entry chunk. It returns a module table with
  the hooks (`disc_initialize`, `frame_update`, `frame_render`,
  `disc_shutdown`). It reads the scene through the drive (`pdk.cd_scene_object_read`)
  and draws in this order: sound, world meshes, lamps, board.
- `scripts/example-lua-math.lua` - the camera loop. Every step is rounded to
  float32 (`f()`) in the operation order of `example-cpp-route.cpp`, which is
  why the frames match the C++ disc bit for bit.
- `scripts/example-lua-draw.lua` - the model put shared by the world, board and lamps.
- `scripts/example-lua-board.lua`, `scripts/example-lua-lamps.lua`,
  `scripts/example-lua-sound.lua` - one episode each.
- `scenes/main.scene.toml` - the scene the drive reads; edit it in the editor's
  Scene layout.
- `models/*.glb` - glTF models, baked by the build.
- `assets/snd/*.wav` - the music and the clack, baked to `.pcm`.
- `assets/tex/light_pool.png` - baked, not drawn by the demo; it stays in
  `[textures]` because the manifest rule requires a non-empty section.

## Scripts and state

A chunk local or a field of the module table dies with the code on reload.
Only the `state` table survives, so the entry keeps the scene copies, the frame
counter and the sound's last frame and area there. `state` holds plain numbers,
strings, booleans and tables only; a function or a coroutine in it would keep
the old bytecode alive after a reload.

A per-frame script failure is logged to stderr and the loop keeps running; with
`--dev` the console stops at the first failure so a reload can fix it.

## Memory

`[budget.pccl]` in `disc.toml` sets `script_memory_size` for the whole Lua
machine, bootstrap included. Its comment has the measured figures and why the
budget is 512 KiB.
