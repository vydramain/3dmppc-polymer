# example-cpp-pdkkit

The Solid Maid demo as a C++ disc, and the template of New Project. The
demo loops every 8640 frames (144 s at 60 frames per second) through the
factory, the street and the flat. A frame's camera and board letters are pure
functions of its number in the period; the lamps' lights and the music depend
on earlier frames.

## Where to change what

| Episode | Files | What to change | Apply |
| --- | --- | --- | --- |
| Camera flight: a closed Catmull-Rom spline through the 20 keys | `scenes/main.scene.toml` (`cam_00`..`cam_19`, under `route`, the group with `entity = "route"`), `src/example-cpp-pdkkit-route.cpp` (`rv_route_camera_at`, `ecs_system_route`), `src/example-cpp-pdkkit-route.hpp` (`RV_ROUTE_PERIOD_FRAMES`) | move a `cam_NN` object to change the path; change the spline or the lens in the source | Build and Restart |
| World meshes: factory, machines, street, house, apartment, furniture | `scenes/main.scene.toml`, `src/example-cpp-pdkkit.cpp` (`rv_dmain::read_scene` loads the scene, `draw_world` draws it) | position, rotation or scale of a mesh object; a `hidden` component keeps an object out of the frame | Build and Restart |
| Model geometry: Solid Maid's places 1:1 (`factory.gltf` hall, `machines.gltf` props, honour and roster boards, signs, `street.gltf`, `house.gltf`, `apartment.gltf`, `furniture.gltf`) | `assets/models/*.gltf` with their `.bin`, drawn by the ECS draw of `ecs_draw_entity` (`pdk/kit/include/pdkkit/ecs/rv_ecs_frame.hpp`) | replace the .gltf and its .bin in `assets/models/`; the models are built from Solid Maid's level code, one atlas cell per surface and Solid Maid's tess subdivision; the flat's window has no frame card, since Solid Maid's frame cell has no cut-out texels | Build and Restart |
| Pictures of the models | the PNGs `assets/tex/<model>_<n>.png`, referenced by the `.gltf` files (18 of the 19 models have them; `halo.gltf` has none) | edit the PNG; the build reads it through the `.gltf` and bakes it to `.mppctex` | Build and Restart |
| Depth order | `pdk/kit/include/pdkkit/ecs/rv_ecs_frame.hpp` (`ecs_depth_decal_bias`, `ecs_depth_world_min`, `ecs_depth_world_max`, `ecs_depth_decal_min`, `ecs_depth_decal_max`) | the world takes the ordering-table window `RV_DEPTH_MIN`..`RV_DEPTH_MAX` minus the 260-key decal bias; decals take the window shifted up by the bias, so the board's halos and letters, which are decals, draw over the board | Build and Restart |
| Board "ОСТАЛОСЬ: 5": glyphs, flicker, halo | `src/example-cpp-pdkkit-board.cpp` (`RV_BOARD_FLICKER_TABLE`, `RV_BOARD_FLICKER_STEP`), `src/example-cpp-pdkkit-board.hpp` (`RV_BOARD_CELL_COUNT`, the `flicker` component's description with its `cell` field), and the scene objects `glyph_N` and `halo_N` under `board`, each with its own `cell` field | a letter's place (its object's `position`) and letter (its `mesh`), a halo's place, flicker table, step in frames; `cell` sets only the flicker phase; cell 9 is the space and has no object | Build and Restart |
| Street lamps: each approach of the eye toggles the light | `src/example-cpp-pdkkit-lamps.cpp` (`ecs_system_lamp_light`, `RV_LAMP_REACH`), `src/example-cpp-pdkkit-lamps.hpp` (`ecs_entity_lamp`, `ecs_component_lamp_light`), `lamp_1`..`lamp_4` in the scene (entity `lamp`, mesh `lamp.gltf`), `assets/models/lamp_light.gltf` | reach of a lamp, lamp positions; 4 lamps 8 m apart on both kerbs | Build and Restart |
| Music per area (1-s bars through two slots: factory 3 bars, street 5, home 10), workshop loops, board clack | `src/example-cpp-pdkkit-sound.cpp` (`RV_SOUND_WORKSHOP_MAX_X`, `RV_SOUND_FLAT_MIN_X`, `RV_SOUND_LOOPS`, `RV_SOUND_CLACKS`), `src/example-cpp-pdkkit-music.cpp` (`RV_SOUND_SONGS`), `assets/snd/*.wav` | area borders on the camera's x (workshop x < 0, flat x > 33.8), bar lists, loops | Build and Restart |
| Frame clock and background | `src/example-cpp-pdkkit.cpp` (`frame_update`, `RV_EXAMPLE_CPP_BACKGROUND`) | the clock counts only frames with dt > 0 | Build and Restart |
| Disc setup and budgets | `disc.toml` | sections and `[budget.*]`; `[budget.pccg]` allows 4096 triangles; `[assets]` lists the scene, the WAVs and the `.gltf` models; `[assets.texture]` sets the format their PNG textures are baked in (`idx8`) | Build and Restart |

Terms in the table: an atlas cell is one rectangle of a shared texture image
that a surface samples from. Tess is Solid Maid's mesh subdivision step: it
splits large surfaces into smaller pieces, which keeps the console's affine
texture mapping (no perspective correction) from swimming. Cut-out texels are
transparent pixels of a texture; where they sit, the surface has holes and
the view shows through.

The scene is an ECS (entity-component-system) world. An entity is one object
of the scene; a component is data it carries (`model`, `hidden`, `decal`, or a
disc's own such as `flicker`; an object's `mesh` key fills `model`); a system
is a function the frame runs over the entities that carry a component. A decal
is a flat piece drawn on a surface (the board's halos and letters), in its own
depth window.

Build and Restart is the editor's Restart button (tooltip "Build and
Restart"). Ctrl+B builds, F5 runs. Reload (F8, button "Reload") reloads the
entry Lua script; this C++ disc has none, so the button is off for it. The
drive can refresh a texture asset in place (root README), but this demo's
pictures are not texture assets: the model build reads them through the
.gltf files, so every change here needs Build and Restart. See "What still
needs a restart" in the root README.

## Code layout

- `src/example-cpp-pdkkit.cpp` - `rv_dmain`, the disc. It derives from
  `rv_dmain_base_` (`RV_MPPC_DISC_CPP_DEF`, `pdk/lib/include/pdklib/rv_cppdisc/rv_cppdisc.hpp`)
  and plants itself with `RV_MPPC_DISC_ENTRY_DEF` (`pdk/include/pdk/de/rv_dv.h`).
  It loads the scene into the ECS world (`ecs_scene_load`,
  `pdk/kit/include/pdkkit/ecs/rv_ecs_loads.hpp`), registers the route, lamp
  and flicker types and systems, and draws the world each frame with
  `ecs_world_frame`.
- `src/example-cpp-pdkkit-route.*` - the camera loop: the `route` entity type and `ecs_system_route`.
- `src/example-cpp-pdkkit-board.*`, `src/example-cpp-pdkkit-lamps.*`,
  `src/example-cpp-pdkkit-sound.*` - one episode each; the board and the lamps are
  ECS systems (`ecs_system_flicker`, `ecs_system_lamp_light`).
- `src/example-cpp-pdkkit-music.cpp` - the music: the bars of each area, one voice through two slots.
- `pdk/kit/include/pdkkit/ecs/` - the ECS: the world, the scene loader, the
  frame that draws the models.
- `scenes/main.scene.toml` - the scene the disc loads into the ECS world; edit
  it in the editor's Scene layout.
- `assets/models/*.gltf` + `.bin` - glTF models, baked to `.mppcmod` by the build;
  their pictures are `assets/tex/*.png`, baked to `.mppctex`.
- `assets/snd/*.wav` - Solid Maid's music, loops and the clack, baked to `.pcm`.

The lamp is the example of a disc's own entity. `ecs_entity_lamp`
(`src/example-cpp-pdkkit-lamps.hpp`) adds the `lamp_light` component by default;
`PDKKIT_COMPONENT` names it for the scene, and `rv_dmain::register_ecs` registers
the type, the component and `ecs_system_lamp_light`. The system makes the light
as a child entity of the lamp; the world draws only the entities of scene loads
(`ecs_draw_order_build`, `pdk/kit/include/pdkkit/ecs/rv_ecs_frame.hpp`), so
`ecs_draw_lamps` draws the lights from `draw_world`.

A disc is free to skip `RV_MPPC_DISC_CPP_DEF` and write the five `rv_de`
hooks by hand.
