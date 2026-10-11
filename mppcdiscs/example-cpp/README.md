# example-cpp

The Solid Maid demo as a C++ disc, and the template of New Project. The
demo loops every 1080 frames (18 s at 60 frames per second) through the
factory, the street and the flat. A frame's camera, board letters, lamps and
music are pure functions of its number in the period.

## Where to change what

| Episode | Files | What to change | Apply |
| --- | --- | --- | --- |
| Camera flight: a closed Catmull-Rom spline through the 18 keys | `scenes/main.scene.toml` (`cam_00`..`cam_17`, under `route`), `src/example-cpp-route.cpp` (`rv_route_camera_at`), `src/example-cpp-route.hpp` (`RV_ROUTE_PERIOD_FRAMES`) | move a `cam_NN` object to change the path; change the spline or the lens in the source | Build and Restart |
| World meshes: factory, street, house, apartment | `scenes/main.scene.toml`, `src/example-cpp.cpp` (`read_scene`, `add_mesh`) | position, rotation or scale of a mesh object | Build and Restart |
| Model geometry | `models/*.glb`, drawn by `rv_draw_model` (`src/example-cpp-draw.hpp`) | replace the .glb in `models/` | Build and Restart |
| Pictures of the models | the PNGs embedded in any `models/*.glb` (all sixteen carry them; baked to `<stem>-<n>.mppctex`) | edit the .glb | Build and Restart |
| Board "ОСТАЛОСЬ: 5": glyphs, flicker, halo | `src/example-cpp-board.cpp` (`RV_BOARD_TEXT`, `RV_BOARD_FLICKER_TABLE`, `RV_BOARD_FLICKER_STEP`, `RV_BOARD_HALO_MODEL`), `src/example-cpp-board.hpp`, mesh `board` in the scene | glyph order, flicker table, step in frames | Build and Restart |
| Street lamps: lit on the way out, dark on the way back | `src/example-cpp-lamps.cpp` (`RV_LAMP_REACH`, `RV_LAMP_LIGHT_MODEL`), `lamp_N` meshes in the scene, `models/lamp_light.glb` | reach of a lamp, the light model, lamp positions | Build and Restart |
| Music per area and board clack | `src/example-cpp-sound.cpp` (`RV_SOUND_WORKSHOP_MAX_X`, `RV_SOUND_FLAT_MIN_X`, `RV_SOUND_MUSIC`, `RV_SOUND_CLACK`), `assets/snd/*.wav` | area borders on the camera's x, track files | Build and Restart |
| Frame clock and background | `src/example-cpp.cpp` (`frame_update`, `RV_EXAMPLE_CPP_BACKGROUND`) | the clock counts only frames with dt > 0 | Build and Restart |
| Disc setup and budgets | `disc.toml` | sections and `[budget.*]` | Build and Restart |

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
- `src/example-cpp-draw.hpp` - the model put shared by the world, board and lamps.
- `scenes/main.scene.toml` - the scene the drive reads; edit it in the
  editor's Scene layout.
- `models/*.glb` - glTF models, baked to `.mppcmod` by the build.
- `assets/snd/*.wav` - Solid Maid's music and the clack, baked to `.pcm`.
- `assets/tex/light_pool.png` - baked to `light_pool.mppctex`, which the demo
  does not draw; it stays in `[textures]` because the manifest rule
  (`pdk/lib/include/pdklib/rv_manifest/rv_manifest.cpp`, `validate_required_sections`)
  requires a non-empty section.

A disc is free to skip `RV_MPPC_DISC_CPP_DEF` and write the five `rv_de`
hooks by hand.
