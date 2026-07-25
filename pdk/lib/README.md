# Software Development Kit (SDK) — the disc author's toolbox

> Machine-generated. Every header in this tree carries a `NEUROSLOP` banner:
> written by Claude (claude-opus-5), not reviewed by a human.

Header-only helpers a disc is written *with*. `pdk/` says what the console can
do; `pdklib/` is the arithmetic you would otherwise write again in every game.

## The fourth tree

| Tree | What it is | Who depends on it |
| --- | --- | --- |
| [`pdk/`](../) | the **contract** — the hardware vocabulary | the console **and** every disc |
| `src/` | the **console** — implements the contract | — |
| [`mppcdiscs/`](../../mppcdiscs/) | the **discs** — games | `pdk/` + (optionally) `pdklib/` |
| `pdklib/` | **conveniences for discs**, written *on top of* the contract | discs only, and only if they want to |

`pdklib/` depends on `pdk/` and the standard library. Nothing else — no `src/`, no
SDL. The CMake target is `INTERFACE` with `pdk/lib/include` on the path, and only
disc targets link it.

## Namespaces

One tree, one namespace, and the boundary is visible in every signature:

| Namespace | Whose it is | Where it lives |
| --- | --- | --- |
| `rv_pdk` | the **contract** — the hardware vocabulary | `pdk/include/pdk/**` |
| `rv_pdklib` | **this library** | `pdk/lib/include/pdklib/**` |
| `rv_3dmppc` | the **console**, and only the console | `src/**` |

Everything in these headers is in `rv_pdklib`. Contract types are always written
out — `rv_pdk::rv_primitive`, `rv_pdk::rv_color`, `rv_pdk::rv_texture` — so a
signature says on its face which side of the boundary each type comes from:

```cpp
rv_pdk::rv_primitive out{};
if (rv_pdklib::rv_xform_triangle(conf, vertexes, out)) cv->frame_put(out);
```

**No header here contains a `using namespace`.** A using-directive in a header
leaks into every translation unit that includes it, and this is a library meant
to be included in someone else's code — a disc's, in a file it also fills with
its own names. Discs are free to write `using namespace rv_pdklib;` in their own
`.cpp`; the headers never make that choice for them.

`rv_3dmppc` appears nowhere in this tree. If it ever does, something from the
console has been misfiled — see below.

## Why the console does not link it

Because `pdklib/` is **not part of the contract**. The console implements `pdk/`;
it has no business knowing which maths library a disc chose. If the console ever
needed something from here, that something was misfiled — it belongs in `pdk/`
(if both sides must agree on it) or in `src/` (if it is the machine's own).

The direction is one-way and worth stating plainly: `pdklib/` is built *over* the
contract, never *into* it. Deleting this whole tree must leave the console
byte-identical and every disc still buildable — merely more tedious to write.

The concrete thing it buys: the console has **no 3D**. `rv_vertex` is a pair of
screen `int16`, and projection is entirely the disc's job, exactly as the PSX's
GTE was a coprocessor the programmer drove by hand. `pdklib/` is that hand.

## Why the parsers do not read files

**Nothing in `pdklib/` opens a file.** `rv_obj_parse` takes `const void* data,
size_t size` — bytes, never a path.

Reading is the drive's privilege. A disc calls `rv_cd::asset_open` /
`asset_size` / `asset_read` into a buffer it owns, then hands those bytes to a
parser. A parser that took a filename would grow a second, private file system
beside the one the console models, and a disc could then read something that is
not on the disc at all — the whole medium abstraction would be decoration.

Same reason there is no image loader here: decoding PNG is an **offline
authoring** job (`pdk/tools/`), and what ships on a disc is texels the console can
upload as-is.

## The headers

| Header | What it holds |
| --- | --- |
| `pdklib/rv_math.hpp` | `rv_vec2/3/4`, `rv_mat4`; dot, cross, length, normalize; translate / rotate / scale; matrix and matrix-vector products |
| `pdklib/rv_camera.hpp` | `rv_look_at`, `rv_perspective` (both left-handed), the `rv_camera` bundle, world → clip |
| `pdklib/rv_xform.hpp` | geometry → `rv_primitive`: near-plane rejection, perspective divide, screen mapping with `int16` saturation, screen-space back-face culling, the ordering-table depth key |
| `pdklib/rv_obj.hpp` | Wavefront `.obj` from a memory buffer (`v` / `vt` / `vn` / `f`, fan triangulation, negative indices) |
| `pdklib/rv_color.hpp` | HSV→RGB, lerp / scale / modulate / add, Lambert shading into vertex colours |
| `pdklib/rv_text.hpp` | text: builds the font atlas and palette for upload, lays a string out as one textured quad per glyph, measures it |
| `pdklib/rv_font_data.hpp` | the bitmap font itself — 5×7 ink in an 8×8 cell, ASCII 32..126 plus a notdef block |

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
- Clip space after `rv_perspective`: `w` equals the view-space `z`, and after the
  divide `x, y ∈ [-1, +1]` (`+y` still up), `z ∈ [0, 1]`.
- Screen mapping flips `y` (the framebuffer's `y` grows down) and **saturates**
  into `int16` — it never wraps.
- Ordering-table depth: **larger = nearer**, per the `pdk/` contract. A disc
  picks its own `[depth_min, depth_max]` range; the console clamps.

## Deferred, deliberately

- **Real near-plane clipping.** Today a polygon with any vertex behind the near
  plane is rejected whole, so a large polygon vanishes the instant one corner
  passes the eye. Splitting it into new interpolated vertices is future work.
- **Inverse-transpose normals** under non-uniform scale.
- **Ear-clipping** for concave `.obj` faces (fan triangulation assumes convex).
- **Quaternions**, frustum culling, and any scene graph — a disc that wants those
  is describing a game engine, and this is not one.
