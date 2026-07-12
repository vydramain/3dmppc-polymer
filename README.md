# 3dmppc — PSX-like software rasterizer

A tiny C++23 game/runtime skeleton and the seed of a homemade PSX-style fantasy
console. It renders 3D geometry entirely on the CPU into a 320×240 framebuffer
and presents it through SDL3 with crisp integer scaling.

Target hardware fantasy lives in [`docs/platform/specs.md`](docs/platform/specs.md).
The demo in `src/` is only a **skeleton cartridge** — see [`docs/README.md`](docs/README.md)
for the console-vs-cartridge split.

## Features (day one)

- **C++23**, CMake, no engine — everything is explicit and readable.
- **SDL3** window + presentation only (nearest-neighbour integer upscale).
- **Software rasterizer**: z-buffer, perspective-correct interpolation, per-vertex
  color, nearest-sampled textures (no filtering, PSX-style).
- **Own tiny `.obj` loader** (`v`/`vt`/`vn`, fan-triangulated faces).
- **PNG loading via stb_image** — drop `assets/texture.png` to texture the cube.
- Left-handed math (`+x` right, `+y` up, `+z` forward).

## Layout

```
src/
  math/math.hpp        vectors + 4x4 matrices (LH conventions)
  core/color.hpp       32-bit ARGB color helpers
  core/framebuffer.hpp CPU color + depth buffer
  core/window.{hpp,cpp} SDL3 window / framebuffer presenter
  gpu/mesh.hpp         Vertex / Mesh
  gpu/texture.hpp      RGBA texture, nearest sampling
  gpu/rasterizer.{hpp,cpp}  the triangle rasterizer
  assets/obj_loader.{hpp,cpp}  Wavefront .obj parser
  assets/image.{hpp,cpp}       stb_image -> Texture
  main.cpp             turntable demo (Solidmaid protagonist, cube fallback)
assets/cube.obj        sample geometry (the skeleton demo's own assets)
mppcdiscs/             disc library — the games the console loads
  solid/               the reference game's content (assets, scripts, data)
docs/README.md         console vs cartridge — read this first
docs/platform/         the console: hardware spec, runtime, cartridge format
docs/mppcdisc/solid/   the reference game (Solidmaid) design — ships as a .mppcdisc
```

## Build & run

Dependencies are fetched automatically (SDL3 is built from source if it isn't
installed system-wide; `stb_image.h` is downloaded). A network connection is
needed on the first configure.

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/3dmppc       # Esc or close the window to quit
```

Installing SDL3 system-wide (e.g. `libsdl3-dev`) makes configuration instant —
CMake will prefer it over building from source.

## Notes & next steps

- **OBJ vs tinyobjloader:** the built-in loader is intentionally minimal. To use
  tinyobjloader instead, add it via `FetchContent` and reimplement `loadObj` in
  `src/assets/obj_loader.cpp` — the `Mesh` interface stays the same.
- **Near-plane clipping** is not implemented yet; triangles crossing behind the
  camera are dropped whole. Add proper clip-space clipping when you build larger
  scenes.
- **Backface culling** is off; the z-buffer handles overlap. Turn it on in
  `Rasterizer::drawMesh` once winding is settled.
- **Lua** scripting is planned but intentionally absent on day one.
- Toward the fantasy console: 16-bit color + dithering, paletted textures,
  1 MB VRAM / 2 MB RAM budgeting, gamepad input, memory-card saves (see specs).
