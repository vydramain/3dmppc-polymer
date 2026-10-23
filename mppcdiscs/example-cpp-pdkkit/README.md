# example-cpp-pdkkit

The Solid Maid demo as a C++ disc, and the template of New Project. The
demo loops every 8640 frames (144 s at 60 frames per second) through the
factory, the street and the home zone. The camera is a pure function of
its frame's number in the period; the lamps, the board's flicker and the
music depend on earlier frames.

## Where to change what

| Episode | Files | What to change | Apply |
| --- | --- | --- | --- |
| Camera flight: a closed Catmull-Rom spline through the 20 keys | `scenes/main.scene.toml` (`cam_00`..`cam_19`, children of the `route` object; the `eye` object is the camera), `src/example-cpp-pdkkit-route.cpp` (`ecs_system_route`, `spline`), `src/example-cpp-pdkkit-route.hpp` (`RV_ROUTE_PERIOD_FRAMES`) | move a `cam_NN` object (its `transform`) to change the path; the `lens` component of `eye` (`fov`, `near_plane`, `far_plane`) sets the lens; the spline itself is in the source | Build and Restart |
| World meshes: factory, machines, street, house, apartment, furniture | `scenes/main.scene.toml`, `src/example-cpp-pdkkit.cpp` (`rv_dmain::read_scene` loads the scene, `rv_dmain::frame_render` draws it with `world_frame`) | position, rotation or scale of a mesh object; a `hidden` component keeps an object out of the frame (`ecs_draw_entity` skips it) | Build and Restart |
| Model geometry: Solid Maid's places 1:1 (`factory.gltf` hall, `machines.gltf` props, honour and roster boards, signs, `street.gltf`, `house.gltf`, `apartment.gltf`, `furniture.gltf`) | `assets/models/*.gltf` with their `.bin`, drawn by the ECS draw of `ecs_draw_entity` (`pdk/kit/include/pdkkit/ecs/rv_ecs_frame.hpp`) | replace the .gltf and its .bin in `assets/models/`; the models are built from Solid Maid's level code, one atlas cell per surface and Solid Maid's tess subdivision; the flat's window has no frame card, since Solid Maid's frame cell has no cut-out texels | Build and Restart |
| Pictures of the models | the PNGs `assets/tex/<model>_<n>.png`, referenced by the `.gltf` files (21 of the 22 models have them; `halo.gltf` has none) | edit the PNG; the build reads it through the `.gltf` and bakes it to `.mppctex` | Build and Restart |
| Depth order | `pdk/kit/include/pdkkit/ecs/rv_ecs_frame.hpp` (`ecs_depth_decal_bias`, `ecs_depth_world_min`, `ecs_depth_world_max`, `ecs_depth_decal_min`, `ecs_depth_decal_max`) | the world takes the ordering-table window `RV_DEPTH_MIN`..`RV_DEPTH_MAX` minus the 260-key decal bias; decals take the window shifted up by the bias, so the board's halos and letters, which are decals, draw over the board | Build and Restart |
| Board "ОСТАЛОСЬ: 5": glyphs, flicker, halo | `src/example-cpp-pdkkit-board.cpp` (`ecs_system_flicker`, the `RV_FLICKER_*` timing constants), `src/example-cpp-pdkkit-board.hpp` (`ecs_component_flicker` and its scene fields `cell` and `wear`), and the scene objects `glyph_N` and `halo_N` under `board`, each with its own `flicker` component | a letter's place (its object's `transform`) and letter (its `model` `source`), a halo's place, the timing constants in the source; `cell` seeds the lamp's random cycle, `wear` shortens its steady burn; there is no object with cell 9 | Build and Restart |
| Street lamps: a camera entry into a lamp's zone switches the light | `src/example-cpp-pdkkit-lamps.cpp` (`ecs_system_lamps`, `ecs_system_light`), `src/example-cpp-pdkkit-lamps.hpp` (`ecs_entity_lamp`, `ecs_component_lamp_light`, `ecs_component_zone_lamp`), the scene objects `zone_lamp_N` (`zone`, `zone_lamp`), `lamp_N` (entity `lamp`, `model` `lamp.gltf`) and their children `lamp_N_light` and `lamp_N_pool`, `assets/models/lamp_light.gltf` | a zone's `transform` and `size` (the reach of its lamp), a lamp's `transform`, the lamp a zone names (`zone_lamp` `lamp`); 4 lamps 8 m apart on both kerbs; a step along +X lights the lamp, along -X darkens it | Build and Restart |
| Music per area (factory 3 bars, street 5, home 10; one voice through two slots), the loop, the lamp clicks | `scenes/main.scene.toml` (`zone_music` of `factory`, `street`, `home`; `zone_clack` of `factory`), `src/example-cpp-pdkkit-sound.cpp` (`ecs_system_music`, `ecs_system_clack`, `switch_loop`, `RV_SOUND_CLACKS`), `src/example-cpp-pdkkit-sound.hpp` (`RV_SOUND_SLOTS`, `RV_SOUND_CLACK_COUNT`), `src/example-cpp-pdkkit-music.cpp` (`rv_music_init`, `bar_asset`), `assets/snd/*.wav` | a zone's `zone_music` (`music` names the song, `loop` the loop, empty for none) and its `transform` and `zone.size` (area borders on world X); the bars are `assets/snd/mus_<song>_NN.wav`, counted from 01 until one is missing; `RV_SOUND_CLACKS` lists the four lamp clicks, played once in a frame that has a `flicker_switched`, while the listener stands in `zone_clack` | Build and Restart |
| Frame clock and background | `pdk/kit/include/pdkkit/disc/rv_pdkkit_disc.hpp` (`frame_update`), `src/example-cpp-pdkkit.cpp` (`RV_EXAMPLE_CPP_BACKGROUND`) | the clock counts only frames with dt > 0 | Build and Restart |
| Disc setup and budgets | `disc.toml` | sections and `[budget.*]`; `[budget.pccg]` allows 4096 triangles; `[assets]` lists the scene, the WAVs and the `.gltf` models; `[assets.texture]` sets the format their PNG textures are baked in (`idx8`) | Build and Restart |

Terms in the table: an atlas cell is one rectangle of a shared texture image
that a surface samples from. Tess is Solid Maid's mesh subdivision step: it
splits large surfaces into smaller pieces, which keeps the console's affine
texture mapping (no perspective correction) from swimming. Cut-out texels are
transparent pixels of a texture; where they sit, the surface has holes and
the view shows through.

The scene is an ECS (entity-component-system) world. An entity is one object
of the scene; a component is data it carries (`model`, `hidden`, `decal`, or a
disc's own such as `flicker`); a system is a function the frame runs over the
entities that carry a component. A decal is a flat piece drawn on a surface
(the board's halos and letters), in its own depth window.

Build and Restart is the editor's Restart button (tooltip "Build and
Restart"). Ctrl+B builds, F5 runs. Reload (F8, button "Reload") reloads the
entry Lua script; this C++ disc has none, so the button is off for it. The
drive can refresh a texture asset in place (root README), but this demo's
pictures are not texture assets: the model build reads them through the
.gltf files, so every change here needs Build and Restart. See "What still
needs a restart" in the root README.

## Code layout

- `src/example-cpp-pdkkit.cpp` - `rv_dmain`, the disc. It derives from
  `rv_pdkkit::pdkkit_disc` (`pdk/kit/include/pdkkit/disc/rv_pdkkit_disc.hpp`)
  and plants itself with `RV_MPPC_DISC_ENTRY_DEF` (`pdk/include/pdk/de/rv_dv.h`).
  It loads the scene into the ECS world (`scene_load`) and, in `register_ecs`,
  registers the lamp entity type, the components and the systems in frame order;
  it draws the world each frame with `world_frame`.
- `src/example-cpp-pdkkit-route.*` - the camera loop: `ecs_component_route_key`,
  `ecs_component_route_rider`, `ecs_component_trail` and `ecs_system_route`.
- `src/example-cpp-pdkkit-zones.*` - the zones: `ecs_component_zone`,
  `ecs_component_zone_crossed` and `ecs_system_zones`, which marks each entry of a rider.
- `src/example-cpp-pdkkit-lamps.*` - the street lamps: `ecs_entity_lamp`,
  `ecs_component_lamp_light`, `ecs_component_zone_lamp`, `ecs_system_lamps`, `ecs_system_light`.
- `src/example-cpp-pdkkit-board.*` - the board's lamps: `ecs_component_flicker`,
  `ecs_component_flicker_switched`, `ecs_system_flicker`.
- `src/example-cpp-pdkkit-sound.*` - the listener and the sound: `ecs_component_listener`,
  `ecs_component_zone_music`, `ecs_component_zone_clack`, `ecs_system_music`, `ecs_system_clack`.
- `src/example-cpp-pdkkit-music.cpp` - the music bars: `rv_music_init` measures the songs,
  `rv_music_update` plays them through the two slots.
- `pdk/kit/include/pdkkit/ecs/` - the ECS: the world, the scene loader, the
  frame that draws the models.
- `scenes/main.scene.toml` - the scene the disc loads into the ECS world; edit
  it in the editor's Scene layout.
- `assets/models/*.gltf` + `.bin` - glTF models, baked to `.mppcmod` by the build;
  their pictures are `assets/tex/*.png`, baked to `.mppctex`.
- `assets/snd/*.wav` - Solid Maid's music, loops and the clacks, baked to `.pcm`.

The lamp is the example of a disc's own entity. `ecs_entity_lamp::add_defaults`
adds the `lamp_light` component to each scene object with `entity = "lamp"`;
`rv_dmain::register_ecs` registers the type, the component and the lamp systems.
The lights are scene objects (`lamp_N_light`, `lamp_N_pool`), children of the lamp
and hidden in the scene; `ecs_system_light` shows and hides them. The frame draws
every entity with a model that is not hidden (`ecs_draw_order_build` and
`ecs_system_draw`, `pdk/kit/include/pdkkit/ecs/rv_ecs_frame.hpp`), so the lights need no draw code of the disc.

A disc is free to skip `RV_MPPC_DISC_CPP_DEF` and write the five `rv_de`
hooks by hand.
