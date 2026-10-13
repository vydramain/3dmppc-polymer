# example-cpp

The Solid Maid demo as a C++ disc, and the template of New Project. The
demo loops every 4320 frames (72 s at 60 frames per second) through the
factory, the street and the flat. A frame's camera, board letters, lamps and
music are pure functions of its number in the period.

## Where to change what

| Episode | Files | What to change | Apply |
| --- | --- | --- | --- |
| Camera flight: a closed Catmull-Rom spline through the 20 keys | `scenes/main.scene.toml` (`cam_00`..`cam_19`, under `route`), `src/example-cpp-route.cpp` (`rv_route_camera_at`), `src/example-cpp-route.hpp` (`RV_ROUTE_PERIOD_FRAMES`) | move a `cam_NN` object to change the path; change the spline or the lens in the source | Build and Restart |
| World meshes: factory, machines, street, house, apartment, furniture | `scenes/main.scene.toml`, `src/example-cpp.cpp` (`read_scene`, `add_mesh`) | position, rotation or scale of a mesh object | Build and Restart |
| Model geometry: Solid Maid's places 1:1 (`factory.glb` hall, `machines.glb` props, honour and roster boards, signs, `street.glb`, `house.glb`, `apartment.glb`, `furniture.glb`) | `models/*.glb`, drawn by `rv_draw_model` (`src/example-cpp-draw.hpp`) | replace the .glb in `models/`; the models are built from Solid Maid's level code, one atlas cell per surface and Solid Maid's tess subdivision; the flat's window has no frame card, since Solid Maid's frame cell has no cut-out texels | Build and Restart |
| Pictures of the models | the PNGs embedded in the `models/*.glb` (17 of the 18 carry them; `halo.glb` has none) | edit the .glb; the build bakes the pictures to `.mppctex` | Build and Restart |
| Depth order | `src/example-cpp-draw.hpp` (`RV_DRAW_DEPTH_WORLD_MIN`, `RV_DRAW_DEPTH_WORLD_MAX`, `RV_DRAW_DECAL_BIAS`) | the world spans the ordering-table window `RV_DEPTH_MIN`..`RV_DEPTH_MAX` minus Solid Maid's decal bias; the board's halos and letters are lifted by it | Build and Restart |
| Board "ОСТАЛОСЬ: 5": glyphs, flicker, halo | `src/example-cpp-board.cpp` (`RV_BOARD_TEXT`, `RV_BOARD_FLICKER_TABLE`, `RV_BOARD_FLICKER_STEP`, `RV_BOARD_HALO_MODEL`), `src/example-cpp-board.hpp`, mesh `board` in the scene | glyph order, flicker table, step in frames | Build and Restart |
| Street lamps: lit on the way out, dark on the way back | `src/example-cpp-lamps.cpp` (`RV_LAMP_REACH`, `RV_LAMP_LIGHT_MODEL`), `lamp_1`..`lamp_4` meshes in the scene, `models/lamp_light.glb` | reach of a lamp, the light model, lamp positions; 4 lamps 8 m apart on both kerbs | Build and Restart |
| Music per area (1-s bars through two slots: factory 3 bars, street 5, home 10), workshop loops, footsteps, board clack | `src/example-cpp-sound.cpp` (`RV_SOUND_WORKSHOP_MAX_X`, `RV_SOUND_FLAT_MIN_X`, `RV_SOUND_LOOPS`, `RV_SOUND_CLACK`), `src/example-cpp-music.cpp` (`RV_SOUND_SONGS`), `src/example-cpp-steps.cpp` (`RV_STEP_ASSETS`, `RV_STEP_STRIDE`), `assets/snd/*.wav` | area borders on the camera's x (workshop x < 0, flat x > 33.8), bar lists, loops, footstep pairs, step stride | Build and Restart |
| Frame clock and background | `src/example-cpp.cpp` (`frame_update`, `RV_EXAMPLE_CPP_BACKGROUND`) | the clock counts only frames with dt > 0 | Build and Restart |
| Disc setup and budgets | `disc.toml` | sections and `[budget.*]`; `[budget.pccg]` allows 4096 triangles; `[textures]` has no files and keeps only the format its models' textures are baked in (`idx8`) | Build and Restart |

Terms in the table: an atlas cell is one rectangle of a shared texture image
that a surface samples from. Tess is Solid Maid's mesh subdivision step: it
splits large surfaces into smaller pieces, which keeps the console's affine
texture mapping (no perspective correction) from swimming. Cut-out texels are
transparent pixels of a texture; where they sit, the surface has holes and
the view shows through.

Build and Restart is the editor's Restart button (tooltip "Build and
Restart"). Ctrl+B builds, F5 runs. Reload (F8, button "Reload") reloads the
entry Lua script; this C++ disc has none, so the button is off for it. The
drive can refresh a texture asset in place (root README), but this demo's
pictures live inside its models, so every change here needs Build and
Restart. See "What still needs a restart" in the root README.

## Code layout

- `src/example-cpp.cpp` - `rv_dmain`, the disc. It derives from
  `rv_dmain_base_` (`RV_MPPC_DISC_CPP_DEF`, `pdk/lib/include/pdklib/rv_cppdisc/rv_cppdisc.hpp`)
  and plants itself with `RV_MPPC_DISC_ENTRY_DEF` (`pdk/include/pdk/de/rv_dv.h`).
  It reads the scene through the drive (`rv_cd_scene_object_read`) and draws
  the world through the geometry controller.
- `src/example-cpp-route.*` - the camera loop.
- `src/example-cpp-board.*`, `src/example-cpp-lamps.*`,
  `src/example-cpp-sound.*` - one episode each.
- `src/example-cpp-music.cpp` - the music: the bars of each area, one voice through two slots.
- `src/example-cpp-steps.cpp` - the footsteps, one per 0.95 m of the eye's path.
- `src/example-cpp-draw.hpp` - the model put shared by the world, board and lamps.
- `scenes/main.scene.toml` - the scene the drive reads; edit it in the
  editor's Scene layout.
- `models/*.glb` - glTF models, baked to `.mppcmod` by the build.
- `assets/snd/*.wav` - Solid Maid's music, loops, footsteps and the clack,
  baked to `.pcm`.

A disc is free to skip `RV_MPPC_DISC_CPP_DEF` and write the five `rv_de`
hooks by hand.
