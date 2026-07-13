# Disc loading — build, package, and run a `.mppcdisc`

How a disc goes from source to a running game: build the game into its own
shared object, package it (plus assets) into a single `.mppcdisc` file, and have
the console load it at runtime with `3dmppc solid.mppcdisc`.

This is a **design document** for where the console is headed. Today the disc is
still *compiled into* the console executable (`main.cpp` instantiates
`SolidDisc` directly). Nothing here is implemented yet; it records the target
architecture and the decisions behind it, including the future scripting path.

> Scope: this folds together the two documents the platform README lists as
> planned — `disc-abi.md` (the boundary) and `disc-format.md` (the package).

---

## Goal

```
cmake --build build
  ├── build/3dmppc            the console (game-agnostic executable)
  └── build/solid.mppcdisc    the game (its own .so + assets, one file)

./build/3dmppc solid.mppcdisc  → console loads the disc, runs the game
```

The console never names a concrete game. It takes a disc path from `argv[1]`,
mounts the package, loads the code, and drives it through the disc ABI.

---

## 1. Build graph: the "thickness of the ABI" decision

Before any CMake, decide **who owns the rasterizer**. Today the game calls
console code (`gpu/rasterizer`, `assets/obj_loader`, `assets/image`) that happens
to be compiled into the same binary. Once the disc is a separate `.so`, that code
has to live on one side of the boundary.

| | **Option A: thin ABI** (recommended) | Option B: shared runtime |
| --- | --- | --- |
| What crosses the boundary | only `Disc` + POD structs (`InputState`, `Framebuffer`, `DiscServices`) | also all of `gpu` / `core` / `assets` |
| Rasterizer | compiled **into the disc** (the `.so` is self-contained) | in a separate `libmppc_runtime.so`, linked by both |
| C++ ABI surface | minimal → robust across compilers/flags | large → exe + runtime + disc must all be built identically |
| Code duplication | rasterizer duplicated per disc (it is tiny — fine) | none |
| Mental model | "the cartridge carries everything it needs" | "the disc depends on the console's runtime version" |

For a PSX-like fantasy console, **Option A is the honest choice**: the disc gets
only a raw `Framebuffer&` (pixel buffer) + input + services, and draws with its
own code. The only binary contract is the `Disc` vtable and a couple of POD
structs — exactly what an `extern "C"` factory can keep stable. The sketch below
assumes Option A.

### CMakeLists sketch (proposal, not yet applied)

```cmake
cmake_minimum_required(VERSION 3.24)
project(3dmppc LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- console public headers (ABI + services the disc needs) ---
# The disc needs ONLY headers (the POD contract), not the console's .cpp files.
add_library(mppc_abi INTERFACE)
target_include_directories(mppc_abi INTERFACE ${CMAKE_SOURCE_DIR}/src)

# --- CONSOLE (exe) ---
add_executable(3dmppc
  src/main.cpp
  src/core/window.cpp src/core/audio.cpp src/core/savecard.cpp
  src/gpu/rasterizer.cpp
  src/assets/obj_loader.cpp src/assets/image.cpp src/assets/stb_image_impl.cpp
  src/platform/console.cpp
)
target_link_libraries(3dmppc PRIVATE SDL3::SDL3 mppc_abi ${CMAKE_DL_LIBS})
#                                                          ^^^ dlopen/dlsym: -ldl

# --- DISC solid (.so) ---
# Option A: the disc carries its own rasterizer/loader inside the .so.
add_library(solid MODULE            # MODULE = plugin, loaded only via dlopen
  mppcdiscs/solid/src/solid.cpp
  mppcdiscs/solid/src/player.cpp
  # ... the rest of the game's .cpp ...
  # rasterizer/assets as part of the disc (Option A):
  src/gpu/rasterizer.cpp src/assets/obj_loader.cpp src/assets/image.cpp
  src/assets/stb_image_impl.cpp
)
set_target_properties(solid PROPERTIES PREFIX "" OUTPUT_NAME "disc")  # -> disc.so
target_link_libraries(solid PRIVATE mppc_abi)
target_include_directories(solid PRIVATE ${CMAKE_SOURCE_DIR}/mppcdiscs/solid/src)

# --- PACKAGE .mppcdisc ---
# Build a staging dir: disc.so + manifest + assets, then archive it.
set(DISC_STAGE ${CMAKE_BINARY_DIR}/stage/solid)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/solid.mppcdisc
  DEPENDS solid ${CMAKE_SOURCE_DIR}/mppcdiscs/solid/disc.manifest
  COMMAND ${CMAKE_COMMAND} -E make_directory ${DISC_STAGE}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:solid> ${DISC_STAGE}/disc.so
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/mppcdiscs/solid/assets ${DISC_STAGE}/assets
  COMMAND ${CMAKE_COMMAND} -E copy
          ${CMAKE_SOURCE_DIR}/mppcdiscs/solid/disc.manifest ${DISC_STAGE}/
  # zip archive using CMake itself (cross-platform, no external zip):
  COMMAND ${CMAKE_COMMAND} -E tar "cf" ${CMAKE_BINARY_DIR}/solid.mppcdisc
          --format=zip .
  WORKING_DIRECTORY ${DISC_STAGE}
  COMMENT "Packaging solid.mppcdisc"
)
add_custom_target(disc_solid ALL DEPENDS ${CMAKE_BINARY_DIR}/solid.mppcdisc)
```

Notes on the sketch:

- **`MODULE`, not `SHARED`** — in CMake `MODULE` means "a plugin loaded via
  `dlopen`, never linked against". That is the correct semantics for a disc.
- **`add_custom_command(OUTPUT ...)` + `add_custom_target(... ALL DEPENDS ...)`**
  wires packaging into the Ninja graph: `cmake --build build` builds the `.so`,
  then sees the dependency and packages the `.mppcdisc`. Order is guaranteed by
  `DEPENDS solid`.
- **`cmake -E tar --format=zip`** archives with CMake itself — no external `zip`.
- **Ninja** needs no special setup. It is just the executor of the graph CMake
  generates; custom commands become its edges. The only "Ninja tuning" is
  `-G Ninja` at configure time.

---

## 2. Packaging `.mppcdisc` — the key gotcha

A fundamental constraint to know up front: **`dlopen` cannot load a `.so` from
inside a zip archive.** The OS loader needs a real file on the filesystem (an
inode) to map into memory. So at startup:

```
console opens solid.mppcdisc (a zip)
 -> reads manifest
 -> EXTRACTS disc.so to a temp dir (e.g. /tmp/mppc-xxxx/disc.so)
 -> dlopen("/tmp/mppc-xxxx/disc.so")
```

This costs **one extraction at boot** — milliseconds, once, unnoticeable. It has
**zero** effect on per-frame runtime performance. Options:

- **Extract to a temp dir, then `dlopen`** — simple, portable. Recommended.
- **Linux-only trick**: `memfd_create` + write the `.so` bytes + `dlopen(
  "/proc/self/fd/N")` — load from memory, no disk. Neat, non-portable. Later.
- Keep the `.so` *next to* the archive instead of inside — but that breaks the
  "single-file disc" goal.

For the single self-contained file, the answer is **extract-to-temp before
`dlopen`**.

### Manifest

A small text/JSON file inside the archive, read by the console **first**:

```
id = solid
title = Solidmaid: Alkoldun Vasiliusavich
abi_version = 1
kind = native        # native | script
entry = disc.so      # native: the .so name; script: the .lua entry point
```

`abi_version` is critical: the console checks it and **rejects an incompatible
disc** instead of crashing.

---

## 3. Loading and the ABI handshake

The disc's entry point is a C factory (stable ABI — C has no name mangling, no
vtable layout to disagree on):

```cpp
// in the disc (solid.cpp):
extern "C" rv_3dmppc::Disc* mppc_create_disc(int abi_version) {
    if (abi_version != MPPC_ABI_VERSION) return nullptr;  // handshake
    return new SolidDisc();
}
extern "C" void mppc_destroy_disc(rv_3dmppc::Disc* d) { delete d; }
```

The console:

```cpp
void* h = dlopen(extracted_so_path, RTLD_NOW | RTLD_LOCAL);
auto create  = (create_fn)dlsym(h, "mppc_create_disc");
Disc* disc   = create(MPPC_ABI_VERSION);      // nullptr -> incompatible, bail
// ... console.run(*disc) ...
destroy(disc); dlclose(h);                    // keep the .so open for the whole run
```

- `RTLD_LOCAL` (Option A) keeps the disc's symbols out of the global namespace —
  isolation.
- `main.cpp` no longer `#include`s `game/solid.hpp` or names `SolidDisc`; it takes
  the **disc path from `argv[1]`**. That is what makes "console separate, game
  separate" real rather than aspirational.

Why a C function and not a C++ class across the boundary: the **C++ ABI is
unstable** (name mangling, vtable layout, exception handling differ across
compilers, versions, and flags), so a C++ class is a poor boundary between two
*separate* binaries. `extern "C"` is a narrow, predictable gate; once the console
holds the returned `Disc*`, both sides are inside one binary again and the vtable
is internally consistent.

---

## 4. The scripting future (Lua) — authoring vs. loading

A tempting idea is: *scripts -> generated C++ -> compiled `.so` -> loaded*. Split
it into two questions that are easy to conflate — **when** it happens and **what
it costs**.

**Runtime C++ compilation is the wrong tool.** To "generate a disc at runtime"
that way, the player's machine would need a full C++ toolchain, and every disc
would compile for *seconds*. Huge dependency, slow, fragile. Avoid it.

Separate the concerns:

| Question | Answer |
| --- | --- |
| How a disc is **authored** | transpiling Lua -> C++ -> `.so` is acceptable, but as a **build-time / offline** step on the developer's machine (an AOT optimization) |
| How the console **loads** a disc at runtime | via an **interpreter inside the console**, no compilation |

**The architecture: one ABI, two *kinds* of disc behind it.**

1. **Native disc** (`kind = native`): a compiled `.so` exporting
   `mppc_create_disc`. This is how `solid` works. Maximum speed.
2. **Script disc** (`kind = script`): the `.mppcdisc` holds Lua scripts + assets
   **as data**. The console ships a single built-in `ScriptDisc : public Disc`
   that *is* the implementation — it loads the Lua and, in `update()/render()`,
   calls the script's functions. **No compilation, no `.so` for scripted games.**

Why this fits the "generate a disc at runtime" goal:

- **Runtime disc generation becomes trivial**: a disc is just a zip of Lua text +
  assets. Generating one = writing files into an archive. No compiler at all.
- **Performance is adequate**: the classic split is *hot* code (the rasterizer) in
  C++, *game logic* in Lua. Interpreted Lua handles game logic easily; if it is
  ever too slow, LuaJIT is near-native. This is how PICO-8, LÖVE, and TIC-80 work
  — exactly this genre.
- **Transpile-to-C++ stays available**: if a specific scripted disc wants native
  speed, run the same Lua through an AOT transpiler into a `.so` **at build
  time**. That is an authoring optimization, not a loading mechanism.

```
                       +----------- Disc ABI (extern "C" create) -----------+
console (3dmppc) ------|                                                    |
   dlopen / read kind  |  native: disc.so   (C++ game, e.g. solid)          |
                       |  script: ScriptDisc (built into console) -> .lua   |
                       +----------------------------------------------------+
   disc authoring:  Lua sources --(build-time, optional)--> transpile -> .so
                    Lua sources --(runtime)--> just drop into the .mppcdisc as data
```

---

## Summary / verdict

| Idea | Verdict |
| --- | --- |
| Game in `mppcdiscs/solid/` -> its own `.so` | OK — `add_library(... MODULE)` |
| Archive assets + `.so` -> `.mppcdisc` | OK — `cmake -E tar --format=zip`; **but** the `.so` must be extracted to temp before `dlopen` |
| Console at `build/3dmppc`, run `3dmppc solid.mppcdisc` | OK — `argv[1]` -> `dlopen` -> `mppc_create_disc` |
| Thin ABI (`extern "C"` factory + POD) | Recommended — robust across compilers |
| Runtime: scripts -> generated C++ -> compile `.so` | Not a runtime mechanism; a compiler on the player's machine is bad. For runtime, use an **embedded Lua interpreter** |
| Transpile Lua -> C++ -> `.so` | OK, but as a **build-time** AOT optimization, not runtime |

The separation instinct is right, and a `.so` disc behind a thin `extern "C"` ABI
is a solid foundation. The one thing to relocate: "runtime = interpreter",
"compilation = offline authoring only".

## Open follow-ups

- Nail down the **exact thin-ABI contract**: which POD structs and functions
  cross the `Disc` boundary (`InputState`, framebuffer access, `DiscServices`).
- Specify the **manifest + load procedure** (extract -> dlopen -> handshake ->
  run) in full.
- Design **`ScriptDisc` + Lua** so native and script discs live behind the same
  ABI.
