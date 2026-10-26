# Software Development Kit (SDK) — the disc author's toolbox

Disc-side helpers a disc is written *with*. `pdk/` says what the console can
do; `pdklib/` is the arithmetic and manifest support you would otherwise write
again in every game.

## The fourth tree

| Tree | What it is | Who depends on it |
| --- | --- | --- |
| [`pdk/`](../) | the **contract** — the hardware vocabulary | the console **and** every disc |
| `src/` | the **console** — implements the contract | — |
| [`mppcdiscs/`](../../mppcdiscs/) | the **discs** — games | `pdk/` + (optionally) `pdklib/` |
| `pdklib/` | **conveniences**, written *on top of* the contract | the console and mppcburner (link `3dmppc_pdklib`), the editor (through `3dmppc_pdkkit`), mppcmodbaker, mppctexbaker and mppcmusbaker (compile or include it); a disc only if it wants to |

`pdklib/` depends on `pdk/` and the standard library. Nothing else: no `src/`, no SDL,
and no cgltf, except the glTF reader sources (below). The glTF reader's sources (`rv_models/rv_mppcmod_gltf*.cpp`) need cgltf, so
`cmake/pdklib.cmake` leaves them out of `3dmppc_pdklib`; mppcmodbaker and the editor compile
them themselves, with cgltf from `third_party/cgltf`. The `3dmppc_pdklib` target exposes
`pdk/lib/include` publicly and compiles the rest of `pdklib/*.cpp`, the manifest parser among
them. The console (`3dmppc`, root `CMakeLists.txt`) and mppcburner link it, so they share one
parser copy.

A burned disc does **not** link it. mppcburner hands the generated disc project
`pdk/lib/include` as an include directory and nothing else, so a disc gets the
header-only modules and not `rv_manifest_*`.

## Namespaces

One tree, one namespace, and the boundary is visible in every signature:

| Namespace | Whose it is | Where it lives |
| --- | --- | --- |
| `rv_pdk` | the **contract** — the hardware vocabulary | `pdk/include/pdk/**` |
| `rv_pdklib` | **this library** | `pdk/lib/include/pdklib/**` |
| `rv_3dmppc` | the **console**, and only the console | `src/**` |

Everything in these headers is in `rv_pdklib`. Contract types are always written
out — `rv_primitive`, `rv_color`, `rv_texture` — so a
signature says on its face which side of the boundary each type comes from:

```cpp
rv_primitive out{};
if (rv_pdklib::rv_xform_triangle(conf, vertexes, out)) rv_cv_frame_put(cv, out);
```

**No header here contains a `using namespace`.** A using-directive in a header
leaks into every translation unit that includes it, and this is a library meant
to be included in someone else's code — a disc's, in a file it also fills with
its own names. Discs are free to write `using namespace rv_pdklib;` in their own
`.cpp`; the headers never make that choice for them.

`rv_3dmppc` appears nowhere in this tree. If it ever does, something from the
console has been misfiled — see below.

## Why the contract does not depend on it

Because `pdklib/` is **not part of the contract**. The console implements `pdk/` and
also links `pdklib/`: its software `rv_cg` backend (`src/rv_pconsole/cg/rv_pccg_sw_view.cpp`)
runs the integer geometry of `rv_gte/`, and `rv_pccg_sw.cpp` asserts that its `rv_cg`
structs match the `rv_gte` types. If a contract header needed something from here, that
something was misfiled: it belongs in `pdk/` (if both sides must agree on it) or in `src/`
(if it is the machine's own).

The direction is one-way: `pdklib/` sits above `pdk/`, and the console, mppcburner and the editor (through `pdkkit`) link it.
`pdklib/` is built *over* the contract, never *into* it: no header in `pdk/include`
includes `pdklib/`. Deleting this whole tree would stop the console and the tools from
building, but the contract would be unchanged, and a disc could still build on `pdk/` alone.

The concrete thing it buys: `rv_vertex` is still a pair of screen `int16`. The 3D
stages live in the console's geometry controller, `rv_cg` (`pdk/include/pdk/cg/rv_cg.h`,
the GTE role of the PSX): it transforms a model by its world matrix and the camera's
view, cuts triangles at the near plane, culls them and puts them into the frame of
`rv_cv`, which does not change. `pdklib/` holds the integer mirror of those stages
(`rv_gte/`), which the console's software backend runs and a disc may run the same way.

## Why the disc-side parsers do not read files

**Nothing a disc can call opens a file.** `rv_mppctex_parse` takes a
`std::span<const std::byte>` — bytes, never a path.

Reading is the drive's privilege. A disc calls `rv_cd::asset_open` /
`asset_size` / `asset_read` into a buffer it owns, then hands those bytes to a
parser. A parser that took a filename would grow a second, private file system
beside the one the console models, and a disc could then read something that is
not on the disc at all — the whole medium abstraction would be decoration.

`rv_manifest_load` is the one function here that takes a path and opens an
`std::ifstream`, and it is the exception that shows where the line runs: it
exists for mppcburner, on a developer's machine, where there is no drive to ask.
A disc cannot reach it — the generated disc project links no pdklib object code
at all. The parsing itself still takes bytes: `rv_manifest_parse` is what any
caller with a buffer uses.

Same reason there is no image loader here: decoding PNG is an **offline
authoring** job (`pdk/tools/`), and what ships on a disc is texels the console can
upload as-is.

The baked model is a file on the disc too: the integer `.mppcmod` layout
(`rv_models/rv_mppcmod_int.hpp`), which `rv_cg` puts.

## The headers

| Header | What it holds |
| --- | --- |
| `pdklib/rv_math/rv_math.hpp` | `rv_vec2/3/4`, `rv_mat4`; dot, cross, length, normalize; translate / rotate / scale; matrix and matrix-vector products |
| `pdklib/rv_camera/rv_camera.hpp` | `rv_look_at`, `rv_perspective` (both left-handed), the `rv_camera` bundle, world → clip |
| `pdklib/rv_math/rv_xform.hpp` | geometry → `rv_primitive`: near-plane rejection, perspective divide, screen mapping with `int16` saturation, screen-space back-face culling, the ordering-table depth key |
| `pdklib/rv_color/rv_color.hpp` | HSV→RGB, lerp / scale / modulate / add, Lambert shading into vertex colours |
| `pdklib/rv_font/rv_font.hpp` | text: builds the font atlas and palette for upload, lays a UTF-8 string out as one textured quad per glyph, measures it |
| `pdklib/rv_font/rv_font_data.hpp` | the bitmap font itself — 5×7 ink in an 8×8 cell, ASCII 32..126 plus a notdef block, and the mask of optional blocks |
| `pdklib/rv_font/rv_font_cyrillic.hpp` | the Cyrillic block (U+0410..U+044F, Ё, ё), added to an atlas with `rv_font_block_cyrillic`; redrawn after PxPlus IBM VGA 9x16, CC BY-SA 4.0 (`third_party/pxplus-ibm-vga/ORIGIN.md`) |
| `pdklib/rv_dscript/rv_dscript.hpp` | `RV_MPPC_DISC_LUA_DEF`; the whole rv_de-hook-to-Lua forwarding, a fixed title and a post-`frame_render` flush. Defines the class only: the disc plants it with pdk's `RV_MPPC_DISC_ENTRY_DEF` |
| `pdklib/rv_cppdisc/rv_cppdisc.hpp` | `RV_MPPC_DISC_CPP_DEF`; a base class a hand-written C++ disc derives from for the startup guards, MENU-button press-edge tracking, `read_asset()`, the screen's size and the frame plumbing (`frame_begin`/`frame_end`, `texture_resident`, `draw_sprite`) every such disc repeats |
| `pdklib/rv_manifest/rv_manifest.hpp` | manifest data model plus parse / load / render / validate entry points; the parser implementation is compiled into `3dmppc_pdklib` |
| `pdklib/rv_gte/rv_gte_matrix.hpp` | integer transform: `rv_gte_matrix` (row-major 4.12 `r`, Q20.12 `t`), compose, point and vertex products |
| `pdklib/rv_gte/rv_gte_trs.hpp` | integer twin of `rv_scene_local_trs`: T * Ry * Rx * Rz * S, angles snapped to 1/4096 turn |
| `pdklib/rv_gte/rv_gte_trig.hpp` | integer sin and cos: 4096 steps to a turn, 4.12 results |
| `pdklib/rv_gte/rv_gte_camera.hpp` | integer camera in Q20.12, the `rv_camera_make` of `rv_camera.hpp` |
| `pdklib/rv_gte/rv_gte_project.hpp` | integer stages of `rv_xform`: near-plane cut, screen projection, back-face cull, depth key, the console's int16 vertex |
| `pdklib/rv_models/rv_mppcmod.hpp` | the `.mppcmod` container: header offsets, record and name rules, the float model |
| `pdklib/rv_models/rv_mppcmod_int.hpp` | the integer `.mppcmod` layout: validate, parse, write |
| `pdklib/rv_models/rv_mppcmod_quant.hpp` | float to integer model: each value to Q.16 once, then the largest vertex shift that fits int16 |
| `pdklib/rv_models/rv_mppcmod_gltf_trs.hpp` | glTF node transforms in int64; header-only |
| `pdklib/rv_models/rv_mppcmod_gltf.hpp`, `rv_mppcmod_gltf_accessor.hpp`, `rv_mppcmod_gltf_image.hpp` | the glTF 2.0 reader with its accessor and image parts (and their `.cpp`); compiled by mppcmodbaker and the editor, never by the console |

Text deserves a word, because its absence from the contract is deliberate. The
console has no text primitive and never will: on the machine this imitates a
font was an asset the game shipped, not something the GPU knew about. So the
font is data, the atlas is an ordinary texture, and a string is a row of quads —
which is also why colour comes from swapping the palette rather than from a
vertex colour the contract cannot yet modulate.

## Conventions fixed here

- **Left-handed**, matching the project: `+x` right, `+y` up, `+z` forward.
- `rv_mat4` is **row-major storage** (`m[row][column]`) applied to **column
  vectors** on the right: `v' = M * v`, translation in the last column,
  composition read right-to-left — `mvp = mul(projection, mul(view, model))`.
- `rv_gte_matrix` (`rv_gte/rv_gte_matrix.hpp`) follows the same row-major, column-vector
  rule, `r[row * 3 + col]`, but keeps the translation apart in `t`, not in a last column.
- Clip space after `rv_perspective`: `w` equals the view-space `z`, and after the
  divide `x, y ∈ [-1, +1]` (`+y` still up), `z ∈ [0, 1]`.
- Screen mapping flips `y` (the framebuffer's `y` grows down) and **saturates**
  into `int16` — it never wraps.
- Ordering-table depth: **larger = nearer**, per the `pdk/` contract. A disc
  picks its own `[depth_min, depth_max]` range; the console clamps.

## Deferred, deliberately

- **Near-plane clipping in the float path.** `rv_xform_near_reject` (`rv_xform.hpp`)
  still rejects a whole polygon when any vertex is behind the near plane, so it
  vanishes the instant one corner passes the eye. The integer path already cuts:
  `rv_gte_near_clip` (`rv_gte/rv_gte_project.hpp`), used by the console's
  `rv_pccg_sw_clip_near`.
- **Inverse-transpose normals** under non-uniform scale.
- **Quaternions**, frustum culling, and any scene graph — a disc that wants those
  is describing a game engine, and this is not one.
