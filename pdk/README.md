# Phantasy Development Kit (PDK)

The **devkit** for the **3DMPPC** — *3D Math Prime Phantasy Console*, a fantasy
console in the spirit of the Sony PlayStation (PSX).

PDK is the **contract** between the console and the games that run on it. It is
neither the console nor a game: it is the third thing both sides depend on — the
headers a game is written against, exactly like a real console ships a devkit of
headers rather than the firmware source.

> Scope: PDK defines *the boundary*. How a disc is packaged and loaded at runtime
> (`.mppcdisc`, `dlopen`, extract-to-temp, `abi_version` handshake) is a separate
> concern documented in [`../docs/platform/disc-loading.md`](../docs/platform/disc-loading.md).

---

## Purpose — why this directory exists

The console must be **game-agnostic**: it has its own life cycle and just lives by
it, never knowing *which* game is inserted. A game must **not** reach into the
console's internals to run — it should only ask the console, as a fantasy machine,
to do console things (draw, play sound, read input, read the disc).

That leaves exactly one thing both sides must share: **a vocabulary of what the
console can do**. Putting that vocabulary in `src/` (next to the rasterizer, the
SDL window, the audio mixer) is what forces a game to "import the console." PDK
exists to pull that vocabulary **out** into its own tree, so that:

- a game is built against **PDK only** — never against console implementation;
- the console **implements** PDK — it is the concrete hardware behind the contract;
- the console and every disc can be **built separately**;
- the boundary is enforced by the build, not by discipline (see *Separation*).

The goal, restated: *the game plays **on** the console; it does not link **into** it.*

---

## The three trees

```
   pdk/                    src/                     mppcdiscs/<game>/
   (devkit: the contract)  (console: the machine)   (games: disc code)
   — abstract only —       — concrete + runtime —    — implement the entry —
        ▲        ▲                                          │
        │        └──────────── implements ──────────────┐  │
        │                     (src depends on pdk)       │  │
        └───────────────── depends on ───────────────────┘◄─┘
                          (a game depends on pdk ONLY)

   Every arrow points inward, to pdk/. There is NO arrow between src/ and
   mppcdiscs/: the console never names a game, a game never names the console.
```

---

## Core principles

1. **The console is game-agnostic.** `src/` boots the hardware, runs a frame loop,
   and drives whatever disc is inserted. It contains zero game names.
2. **A game depends on PDK only.** It is written against the abstract contract and
   never sees `rv_Rasterizer`, the SDL window, or any concrete console class.
3. **Fixed hardware ⇒ one façade.** A fantasy console is a *spec*, not open-ended
   software — its subsystems do not grow organically. So the game talks to **one**
   thing, the console, through a single organizer that vends the subsystems.
   (This is why we chose a façade over N independently-linked interfaces.)
4. **Implementation stays private.** Because the game only ever holds the abstract
   organizer, the concrete backends (`rv_Rasterizer`, the SDL mixer, the file-backed
   memory card) live entirely in `src/`, are never virtual across the boundary, and
   never appear in PDK.

---

## Architecture — the organizer and its controllers

The root of the contract is **`rv_pdko`** — the *Phantasy Development Kit
Organizer*. It is the single handle a disc receives, and it vends the console's
subsystem **controllers**. A disc never constructs a controller; it asks the
organizer for one.

| Symbol     | File          | Subsystem                         | Example surface                       |
| ---------- | ------------- | --------------------------------- | ------------------------------------- |
| `rv_pdko`  | `rv_pdko.hpp` | organizer / façade                | `audio()`, `video()`, `io()`, `disk()`|
| `rv_ca`    | `rv_ca.hpp`   | **C**ontroller **A**udio (SPU)    | `playSfx()`, `setMood()`              |
| `rv_cv`    | `rv_cv.hpp`   | **C**ontroller **V**ideo (GPU)    | `clear()`, `drawMesh()` / primitives  |
| `rv_cio`   | `rv_cio.hpp`  | **C**ontroller **I**nput/**O**utput | `input()`, `cardRead()`, `cardWrite()` |
| `rv_cd`    | `rv_cd.hpp`   | **C**ontroller **D**isk (CD-ROM)  | read assets/code from the inserted disc|

Subsystem split, PSX-faithful:

- **`rv_cd`** is the *game medium* — the read-only optical disc. It streams assets
  and code out of the mounted `.mppcdisc` on demand.
- **`rv_cio`** is the *read/write* I/O: gamepads (input) **and** the memory card
  (persistent save). Save lives here because it is read/write, unlike the disc.

### Class realization — abstract in `pdk/`, concrete at the edges

```
  ╔══════════════════════════ pdk/  (PHANTASY DEV KIT) ══════════════════════════╗
  ║  PURELY ABSTRACT classes + POD. No implementation.                           ║
  ║                                                                              ║
  ║                              ┌───────────────┐                               ║
  ║                              │    rv_pdko    │  organizer / façade           ║
  ║                              │  audio(): rv_ca& │                            ║
  ║                              │  video(): rv_cv& │                            ║
  ║                              │  io()   : rv_cio&│                            ║
  ║                              │  disk() : rv_cd& │                            ║
  ║                              └───────────────┘                               ║
  ║        ┌───────────────┬───────────┴───────┬───────────────┐                 ║
  ║   ┌────┴───┐      ┌────┴───┐         ┌──────┴───┐     ┌─────┴──┐              ║
  ║   │ rv_ca  │      │ rv_cv  │         │  rv_cio  │     │ rv_cd  │              ║
  ║   │playSfx │      │clear   │         │input     │     │read    │              ║
  ║   │setMood │      │drawMesh│         │cardRead  │     │assets  │              ║
  ║   └────────┘      └────────┘         │cardWrite │     └────────┘              ║
  ║                                      └──────────┘                            ║
  ║   POD vocabulary: Color, Vec2/3, Mat4, InputState, enum Sfx/Mood, (Mesh?)     ║
  ║   + the disc-entry interface the game implements (see "Two directions")       ║
  ╚══════════════════════════════════════════════════════════════════════════════╝
                      △  implements                          △  implements
        ┌─────────────┴──────────────────┐        ┌──────────┴────────────────┐
        │  src/  (THE CONSOLE)           │        │  mppcdiscs/<game>/         │
        │                               │        │                            │
        │  rv_Console : rv_pdko         │        │  <Game>Disc : entry        │
        │   owns the loop + controllers:│        │    rv_pdko* pdk_;          │
        │   ├ SoftVideo  : rv_cv        │        │    // boot:  pdk_ = &pdko  │
        │   │   └ rv_Rasterizer,        │        │    // render:              │
        │   │     rv_Framebuffer  (priv)│        │    //   pdk_->video()      │
        │   ├ SdlAudio   : rv_ca        │        │    //        .drawMesh(…)  │
        │   ├ DesktopIo  : rv_cio       │        │    // update:              │
        │   └ DiscMedium : rv_cd        │        │    //   pdk_->io().input() │
        └───────────────────────────────┘        └────────────────────────────┘
         concrete backends are PRIVATE to          a game sees only rv_pdko and
         src/, never virtual, never in pdk/         its controllers — no src/ type
```

---

## Two directions of the contract

PDK holds **two** interfaces, and they must not be merged — they point opposite ways.

| Interface                | Implemented by | Called by | Meaning                                                     |
| ------------------------ | -------------- | --------- | ----------------------------------------------------------- |
| `rv_pdko` (+ controllers)| the console    | the game  | **the hardware** — GPU, SPU, I/O, disc; unified behind one façade |
| the disc-entry interface | the game       | the console | **the cartridge's pins** — `boot` / `update` / `render` hooks the console drives |

The disc-entry interface is the counterpart of `rv_pdko`: the console owns the
frame loop and must call *into* the game each frame. It is what today lives as
`rv_Disc` (`boot` / `update` / `render` / `finished` / `title`) in
`src/platform/disc.hpp`; conceptually it belongs in `pdk/` alongside `rv_pdko`, so
that a disc implements **one** PDK type and calls **one** PDK type. See *Open
decisions*.

---

## Runtime — handshake and a frame

```
  loader (today: constructed in main.cpp; tomorrow: dlopen from a .mppcdisc)
    │
    │ 1. build the CONCRETE console (it constructs its own controllers)
    ├──────────►  rv_Console console;     // : rv_pdko, owns rv_ca/rv_cv/rv_cio/rv_cd
    │
    │ 2. obtain the disc (its code implements the disc-entry interface)
    ├──────────►  entry& disc;
    │
    │ 3. HANDSHAKE: the console hands ITSELF to the disc as rv_pdko&
    ├──────────►  disc.boot(console);     ──►  the disc stashes rv_pdko* pdk_
    │
    │ 4. frame loop (owned by the console — it just lives by it):
    │   ┌────────────────────────────────────────────────────────────────┐
    │   │ console: io.pollHardware()          // SDL → fill input          │
    │   │ disc.update(dt)       ──► game: pdk_->io().input(); … logic      │
    │   │ disc.render()         ──► game: pdk_->video().clear();           │
    │   │                                   pdk_->video().drawMesh(…);     │
    │   │ console: video.present()            // framebuffer → window      │
    │   └────────────────────────────────────────────────────────────────┘
    │        ▲ console calls the disc (entry)   ▼ disc calls the console (rv_pdko)
    │                    two opposite arrows — the whole thing hangs on them
    │
    │ 5. teardown: disc.finished() → leave loop, destroy the disc
    └──────────►  (future: dlclose, clean up the temp extraction)
```

---

## Ownership and lifetime

- The controllers (`rv_ca` / `rv_cv` / `rv_cio` / `rv_cd`) are **owned by the
  concrete console** and live as long as it does.
- The organizer **vends references**, not values: `rv_pdko::video()` returns
  `rv_cv&`. Returning an abstract base by value would slice off the concrete
  implementation — the accessors must hand back references (or pointers).
- A disc holds **only** a `rv_pdko*` / controller references. It never owns, copies,
  or destroys them.

---

## Separation — enforced by the build, not by discipline

PDK is a header-only interface target. The include paths make the boundary
physically impassable:

```cmake
add_library(mppc_pdk INTERFACE)
target_include_directories(mppc_pdk INTERFACE ${CMAKE_SOURCE_DIR}/pdk)  # ONLY pdk/

# console: sees pdk AND its own internals
target_link_libraries(3dmppc PRIVATE mppc_pdk)
target_include_directories(3dmppc PRIVATE src)

# a game: sees pdk ONLY. It is not given a path into src/, so it physically
# cannot write #include "gpu/rasterizer.hpp" — the compiler refuses.
target_link_libraries(<game> PRIVATE mppc_pdk)
```

A game not compiling because it reached for a console header is the feature, not a
bug: the boundary is checked by the toolchain every build.

---

## Naming reference

| Symbol    | Expansion                                    |
| --------- | -------------------------------------------- |
| 3DMPPC    | 3D Math Prime Phantasy Console               |
| PDK       | Phantasy Development Kit                      |
| `rv_pdko` | Phantasy Development Kit **O**rganizer       |
| `rv_ca`   | **C**ontroller **A**udio                     |
| `rv_cv`   | **C**ontroller **V**ideo                     |
| `rv_cio`  | **C**ontroller **I**nput/**O**utput          |
| `rv_cd`   | **C**ontroller **D**isk                      |

(`rv_` is the project-wide namespace prefix, `namespace rv_3dmppc`.)

---

## Open decisions

Tracked here so they are chosen deliberately rather than by drift:

1. **`rv_cv` granularity — how thick is the GPU boundary?**
   - *High-level*: `drawMesh(const Mesh&, const Mat4& mvp, const Texture*, …)`. Mirrors
     the existing `rv_Rasterizer` 1:1, but then `Mesh` / `Texture` **cross the boundary**
     and must move into `pdk/` as POD.
   - *Low-level*: submit primitives / vertices; the game builds and keeps meshes on
     its side, and only POD (numbers) crosses. Thinner `pdk/`, more work.
   - This is the single decision that sets how much POD `pdk/` carries.

2. **Home of the disc-entry interface.** Migrate `rv_Disc` from `src/platform/` into
   `pdk/` so a disc implements one PDK type and calls one PDK type — and rename it to
   fit the PDK scheme, or keep `rv_Disc`.

3. **The POD vocabulary.** Decide the exact set that lives in `pdk/` (`Color`,
   `Vec2/3`, `Mat4`, `InputState`, `Sfx`, `Mood`, and — pending decision #1 — possibly
   `Mesh` / `Texture`).

---

## Status

Template stubs exist (`rv_pdko`, `rv_ca`, `rv_cv`, `rv_cio`, `rv_cd`); method
surfaces are not filled in yet. The console (`src/`) and the reference disc
(`mppcdiscs/solidmaid/`) still use the older in-binary path
(`rv_Disc` + `rv_DiscServices` + a raw framebuffer). Moving them behind `rv_pdko`
is the next step, and depends on the open decisions above.
</content>
</invoke>
