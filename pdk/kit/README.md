# pdkkit: the game framework over pdklib

pdkkit is the optional framework a game is written with: an entity world,
the components an entity carries, the scene that places them, and the frame
that runs the systems. It is headers only (`pdk/kit/include/pdkkit/`,
namespace `rv_pdkkit`), built over `pdklib/`, which is built over `pdk/`.

## What it is, and what it is not

- **It is** headers over `pdklib/` and `pdk/`, and the C++ disc base
  `pdkkit_disc`.
- **It is not** part of the contract. No header in `pdk/include` includes it,
  and `pdklib/` does not include it either.
- **It is not** an object library or a game engine. `3dmppc_pdkkit` is an
  INTERFACE target (`cmake/pdkkit.cmake`): a disc gets headers and no object
  code. Engines are outside it.

The dependency direction is one-way:

```
pdk <- pdklib <- pdkkit <- { a disc with [build] sdk = ["kit"], the editor }
```

The console, the authoring tools and mppcburner do not include
`cmake/pdkkit.cmake`.

## How a disc opts in

The disc adds the key to its `disc.toml`:

```toml
[build]
sdk = ["kit"]
```

The build then adds `pdk/kit/include` to the disc's include path. Without the
key a disc cannot include pdkkit: `#include "pdkkit/..."` fails to compile.

## The frame

`pdkkit_disc` (`disc/rv_pdkkit_disc.hpp`) is the base a disc derives from. It
holds the world (`world_`), the frame counter (`frame_`) and port 0's input
(`input_`).

| Member | What it does |
| --- | --- |
| `disc_initialize` | runs the `rv_cppdisc` base, then registers the built-in components |
| `frame_update` | runs the base, reads port 0 and its press edges, advances `frame_` |
| `scene_load(name, load_number)` | protected; wraps `ecs_scene_load` under the world root |
| `world_frame()` | protected; runs `ecs_world_frame` with this frame's record |

`pdkkit_disc` implements neither `frame_render` nor `disc_shutdown`; the disc
writes them. A disc brackets its own `frame_render` with `frame_begin()` and
`frame_end()` (`pdk/lib/include/pdklib/rv_cppdisc/rv_cppdisc.hpp`). The example,
`mppcdiscs/example-cpp-pdkkit/src/example-cpp-pdkkit.cpp`, derives from
`pdkkit_disc`, registers its systems by name, and calls `scene_load`.

Two rules from the header:

- A disc that hides `disc_initialize` or `frame_update` calls `pdkkit_disc`'s
  version first. Otherwise it silently loses the built-ins, the clock, the input
  and the MENU release edge.
- The world keeps non-owning pointers to entity types. An entity type a disc
  registers has static storage, a namespace-scope object, not a disc member.

## The ECS

An entity is an id into a world of numbered slots with generations. A component
is a plain struct stored per type. A system is a function the frame runs. A
scene load places a scene's entities under a parent. The headers are listed
below.

The frame runs the disc's systems in registration order, then the transform,
the camera, the face camera and the draw of every live entity with a model, in
slot order, system-made entities included. A failing system, the camera or the
face camera ends the frame with its code, and `ecs_frame_run` logs it once per
run of the same failure. A failing transform also ends the frame, and
`ecs_system_transform` logs its own failure on every frame. An unload asked for
during a frame waits for the frame's end, so the frame still draws those
entities. A failed scene load destroys its own entities.

The camera is the one live entity with a `lens` component
(`ecs_component_lens`, `ecs/rv_ecs_component.hpp`). `ecs_system_camera`
(`ecs/rv_ecs_frame.hpp`) builds the frame's camera from its world matrix after
the transform: the eye is the translation, the aim is the z column and the up is
the y column. No lens entity gives `RV_ERR_NOENT`, two give `RV_ERR_ALREADY`; in
both cases nothing is drawn, and the frame logs the refusal by name with its
reason. The disc sets no camera.

Public functions return `int`: `RV_OK` or an `RV_ERR_*` from `pdk/rv_err.h`.
The camera step and the face-camera system (`ecs_system_camera`,
`ecs_system_face_camera`) take an `ecs_reason` out-parameter, which
`ecs_reason_refuse` (`ecs/rv_ecs.hpp`) fills with a refusal text, without an
exception. `rv_scene_parse` and the field parsers (`ecs_field_int`) take a
`std::string &error`.

## Components

`PDKKIT_COMPONENT(T, NAME, fields...)` (`component/rv_component_macro.hpp`)
gives a type its description. Field types are `int`, `fixed`, `fixed3`,
`angle3`, `color`, `string` and `model` (`component/rv_component_desc.hpp`).
A description holds no pointers, because the editor reads it back from the
binary. The macro leaves a record in the disc's ELF section `pdkkit_components`
(`component/rv_component_record.hpp`); its format version is
`ecs_record_format_version`, currently 1. The section is outside the disc hash,
and only the editor reads it (`component/rv_component_records_read.hpp`).

`pdkkit_disc` registers these built-ins before the disc's own:

| Name | Type | Fields |
| --- | --- | --- |
| `name` | `ecs_component_name` | text, a string |
| `transform` | `ecs_component_transform` | `position` fixed3, `rotation` angle3, `scale` fixed3 |
| `model` | `ecs_component_model` | `source` model, `tint` color |
| `camera`, `hidden`, `decal`, `two_sided` | empty structs | none |
| `volume` | `ecs_component_volume` | none; the editor draws it as a wireframe |
| `face_camera` | `ecs_component_face_camera` | none; the model turns to face the camera |
| `lens` | `ecs_component_lens` | fov (degrees), near_plane, far_plane (metres), fixed; defaults 60, 0.1, 100; the frame's camera |

## The scene

A scene is a document in disc.toml's dialect. Its `[scene]` table carries
`version = "M.m"`, the PDK version it was written against
(`scene/rv_scene.hpp`). `rv_version_compatible` decides whether a file is read
(`scene/rv_scene_read.hpp`).
The old `format = 1` header with no `version` is read as 0.0, compatible only
while the PDK major is 0.

Form 2 is current: each `[[object]]` has `id`, `parent` and `entity`, and its
data sits in `[[component]]` blocks. Form 1 objects carry a `kind` and their
data as fields. `scene/rv_scene_migrate.hpp` converts form 1 to form 2 in
memory; the ECS loader calls it, the parser does not.

Numbers are Q20.12 fixed-point values, written as the shortest decimal that
reads back to the same value (`scene/rv_scene_write.hpp`). The writer writes
`version` only, as the editor does. The reader takes the file's bytes, never a
path (`scene/rv_scene.hpp`).

## Sound

`sound/rv_pdkkit_sound.hpp` gives a disc named samples made resident by the
drive, with voice setup, play and stop calls. Samples are bounded by the sound
RAM of `[budget.pcca]` and by the drive's resource table (4096 names, shared
with textures and models; full is RV_ERR_NOMEM). `sound_asset_resident` asks
the drive on every call and gets the same address for a kept name. A refusal
is returned as its code, for example RV_ERR_NOENT, RV_ERR_INVAL or RV_ERR_IO;
the drive logs a refused name once; a failed size measure, table growth or
medium read logs on every attempt.

## Headers

| Header | What it holds |
| --- | --- |
| `component/rv_component_capacity.hpp` | name, value and field capacities shared by scene records and descriptions |
| `component/rv_component_desc.hpp` | component descriptions as constexpr data: field types, defaults, fixed-size names |
| `component/rv_component_field_text.hpp` | parses a field's text into its bytes; writes nothing on refusal |
| `component/rv_component_macro.hpp` | defaults, builders and the `PDKKIT_COMPONENT` macro |
| `component/rv_component_record.hpp` | the record in the `pdkkit_components` section |
| `component/rv_component_records_read.hpp` | reads the records back from a disc image; the editor only |
| `component/rv_component_register.hpp` | the factory and `ecs_component_register<T>` |
| `disc/rv_pdkkit_disc.hpp` | `pdkkit_disc`, the C++ disc base: built-ins, clock, input, world |
| `ecs/rv_ecs.hpp` | the entity world: slots, generations, scene loads, registries, `ecs_reason_refuse` |
| `ecs/rv_ecs_component.hpp` | the built-in components and the factory registry |
| `ecs/rv_ecs_entity.hpp` | entity types |
| `ecs/rv_ecs_face_camera.hpp` | the face-camera system |
| `ecs/rv_ecs_frame.hpp` | the frame record, system registry, camera step, draw and the frame order |
| `ecs/rv_ecs_loads.hpp` | scene loads and unloads, scene queries; `ecs_find_by_name` searches every live entity |
| `ecs/rv_ecs_scene.hpp` | loads a scene asset into the world |
| `ecs/rv_ecs_scene_records.hpp` | the flat records of a form-2 scene |
| `ecs/rv_ecs_store.hpp` | per-type component storage |
| `ecs/rv_ecs_transform.hpp` | the transform system: world matrix per entity |
| `scene/rv_scene.hpp` | the scene document as a disc reads it |
| `scene/rv_scene_migrate.hpp` | in-memory migration from form 1 to form 2 |
| `scene/rv_scene_read.hpp` | the scene loader and its transforms, inline |
| `scene/rv_scene_write.hpp` | the scene writer, the inverse of the parser |
| `sound/rv_pdkkit_sound.hpp` | named samples and voice calls |
