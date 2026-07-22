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
   — abstract + POD —      — concrete + runtime —    — implement the entry —
        ▲        ▲                                          │
        │        └──────────── implements ───────────────┐  │
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
subsystem **controllers** (by pointer). A disc never constructs a controller; it
asks the organizer for one.

| Symbol     | File               | Subsystem                           | Realized surface                                    |
| ---------- | ------------------ | ----------------------------------- | --------------------------------------------------- |
| `rv_pdko`  | `rv_pdko.hpp`      | organizer / façade                  | `audio()`, `video()`, `io()`, `drive()`, `card()`   |
| `rv_ca`    | `ca/rv_ca.hpp`     | **C**ontroller **A**udio (SPU)      | low-level: `sound_asset_malloc`/`sound_asset_write`/`sound_asset_free`, `voice_setup`/`voice_play`/`voice_stop`/`voice_status` |
| `rv_cv`    | `cv/rv_cv.hpp`     | **C**ontroller **V**ideo (GPU)      | low-level: `video_asset_malloc`/`video_asset_write`/`video_asset_free` (textures + palettes), `frame_configure`/`frame_put`/`frame_flush` (primitives, sorted by the hardware ordering table) |
| `rv_cio`   | `cio/rv_cio.hpp`   | **C**ontroller **I**nput/**O**utput | input snapshot (`iport_state`) + capabilities (`iport_abilities`) + mouse (`imouse`) + haptic out (`ohaptic`) |
| `rv_cd`    | `cd/rv_cd.hpp`     | **C**ontroller **D**isk (drive)     | `asset_open` (name → handle) / `asset_size` / `asset_read` into the game's buffer |
| `rv_cm`    | `cm/rv_cm.hpp`     | **C**ontroller **M**emory card      | persistent save slots: `card_slots` (count from console config) / `card_size` / `card_read` / `card_write` (atomic) / `card_erase` |

Shared vocabulary lives next to the controllers: the audio POD is in `ca/`
(`rv_sample`, `rv_voice_conf`, `rv_loop`), the I/O POD is in `cio/` (`rv_isource`,
`rv_istate`, `rv_iaxes`, `rv_imotion`, `rv_imouse`, `rv_ohaptic`), the video POD
is in `cv/` (`rv_color`, `rv_uv`, `rv_vertex`, `rv_texture`, and the primitive
family in `rv_primitives.hpp`), and the cross-controller error enum is
`rv_err.hpp` (see *Error convention*).

Subsystem split, PSX-faithful:

- **`rv_cd`** is the **drive** — the hardware that *reads* the read-only optical
  **disc** (the `.mppcdisc` medium). The accessor is `rv_pdko::drive()`: you talk
  to the drive, the disc is what it reads. It reads assets on demand.

  **Mounting is not part of this contract.** Finding the medium, parsing the
  manifest, `dlopen`ing the disc module and checking its ABI all belong to
  *console initialisation* — the same category as opening the audio device before
  constructing the concrete `rv_ca`. They happen before the game exists, so they
  cannot be operations a game invokes: `rv_cd` has exactly **one** client, the
  game, and therefore no `mount()`/`eject()`. Failures during mounting are not
  `rv_err` either — with no disc booted, the console answers to the *user*
  ("medium unreadable", "disc built for another version"); `rv_err` starts at
  `boot()`.
- **`rv_cio`** is the *live* I/O: gamepad **input** (an instantaneous per-port
  state snapshot, plus the mouse look channel) and haptic **output**. The memory
  card is deliberately NOT here — storage semantics (rare calls, real errors,
  durable state) are the opposite of this snapshot-style contract.
- **`rv_cm`** is the **memory card** — the writable-medium counterpart of the
  drive: the disc is read-only, the card is where a game persists its saves. The
  accessor is `rv_pdko::card()`. The medium is a set of 8 KB **slots** (the slot
  size is ABI — a save blob is designed against it at build time; the slot
  *count* is console configuration, queried at boot via `card_slots()`, default
  16 = 128 KB). The card is always inserted; persistence (the file-backed image)
  is the console's concern, never an operation the game invokes. Writes replace
  a slot whole and are **atomic**: a failed write leaves the old save intact.

### Class realization — abstract in `pdk/`, concrete at the edges

```
  ╔══════════════════════════ pdk/  (PHANTASY DEV KIT) ══════════════════════════╗
  ║  ABSTRACT controller classes + POD. No implementation.                       ║
  ║                                                                              ║
  ║                              ┌───────────────────┐                           ║
  ║                              │    rv_pdko        │  organizer / façade       ║
  ║                              │  audio(): rv_ca*  │                           ║
  ║                              │  video(): rv_cv*  │                           ║
  ║                              │  io()   : rv_cio* │                           ║
  ║                              │  drive(): rv_cd*  │                           ║
  ║                              │  card() : rv_cm*  │                           ║
  ║                              └───────────────────┘                           ║
  ║      ┌───────────────┬───────────────┴───────────────┬───────────────┐       ║
  ║  ┌───┴───┐       ┌───┴───┐       ┌───┴───┐       ┌───┴───┐       ┌───┴───┐   ║
  ║  │ rv_ca │       │ rv_cv │       │ rv_cio│       │ rv_cd │       │ rv_cm │   ║
  ║  │malloc │       │frame_*│       │iport_*│       │asset_*│       │card_* │   ║
  ║  │voice_*│       └───────┘       └───────┘       └───────┘       └───────┘   ║
  ║  └───────┘                                                                   ║
  ║   POD: audio in ca/, i/o in cio/, video in cv/ · rv_err (shared)             ║
  ║   + the disc-entry interface the game implements (see "Two directions")      ║
  ╚══════════════════════════════════════════════════════════════════════════════╝
                      △  implements                          △  implements
        ┌─────────────┴─────────────────┐        ┌───────────┴────────────────┐
        │  src/  (THE CONSOLE)          │        │  mppcdiscs/<game>/         │
        │                               │        │                            │
        │  rv_Console : rv_pdko         │        │  <Game>Disc : entry        │
        │   owns the loop + controllers:│        │    rv_pdko* pdk_;          │
        │   ├ SoftVideo  : rv_cv        │        │    // boot:  pdk_ = &pdko  │
        │   │   └ rv_Rasterizer,        │        │    // render:              │
        │   │     rv_Framebuffer  (priv)│        │    //   pdk_->video()      │
        │   ├ SdlAudio   : rv_ca        │        │    // update:              │
        │   ├ DesktopIo  : rv_cio       │        │    //   pdk_->io()...      │
        │   └ DiscMedium : rv_cd        │        │    //   pdk_->audio()...   │
        └───────────────────────────────┘        └────────────────────────────┘
         concrete backends are PRIVATE to          a game sees only rv_pdko and
         src/, never virtual, never in pdk/         its controllers — no src/ type
```

---

## Audio — two layers (low-level now, sequencer later)

Audio deliberately follows the PSX split of a low-level sound-chip library and a
high-level sequencer built on top of it:

- **Low-level — `ca/rv_ca.hpp` (the SPU).** The realized layer. The game manages a
  private pool of **sound RAM** (`sound_asset_malloc` → `sound_asset_write` sample
  → `sound_asset_free`) and drives a
  fixed set of **voices** (`voice_setup` a config, then `voice_play` / `voice_stop`
  / `voice_status` by voice bitmask). This is the hardware boundary the console
  implements. Sample data crosses as `rv_sample` (POD), voice settings as
  `rv_voice_conf` (POD), loop behaviour as `rv_loop`.

**Deferred within the audio layer** (tracked so they are conscious omissions, not
oversights): per-voice **pitch/playback rate**, **reverb**, **master volume**, and
**ADPCM** sample encoding (raw PCM for now).

### Error convention (`rv_err.hpp`)

Every controller call returns a signed integer, kernel-style: **`>= 0` is success,
a negative value is an `rv_err`**. Calls that yield a value (e.g.
`rv_ca::sound_asset_malloc` returns a sound-RAM address, `rv_cd::asset_read`
returns a byte count) return that value when `>= 0`, or a negative code. Callers
test uniformly with `if (rc < 0) { ... }`.

Codes: `RV_ERR_INVAL` (malformed call — a disc bug), `RV_ERR_NOMEM` (a pool the
controller manages is exhausted), `RV_ERR_BUSY` (resource occupied, retry may
work), `RV_ERR_NOENT` (the named thing does not exist — a content problem, not a
coding one), `RV_ERR_IO` (the device failed to carry out a well-formed call).
Values are ABI: existing codes never change, new ones are appended.

**Signed vs unsigned.** Signed (`int` / `int64_t`) wherever a value shares its
channel with an error code — every method return, and every field that
round-trips through one (`rv_voice_conf::sample_address` holds what
`sound_asset_malloc` returned, so it must be equally wide and equally signed).
Unsigned (`uintN_t`) only for pure data with no error channel, such as
`rv_istate::buttons` and `rv_cio::iport_abilities`, where "nothing" is honestly
`0`. A mask carried by a signed type is defined over bits 0..62 so it can never
be mistaken for an error.

---

## Video — the realized surface (`cv/`)

Video resolved the granularity question the same way audio did — **low-level**,
PSX-faithful in structure:

- **Video RAM.** The game reserves a region (`video_asset_malloc`), uploads
  texture or palette data (`video_asset_write` + `rv_texture`), and keeps only
  the returned opaque address. A palette is an array of 16-bit entries in the
  DIRECT15 texel layout; texel transparency (value `0000h` = fully-transparent
  hole, bit 15 = semi-transparency flag — so no opaque black in textures) is a
  deliberate PSX inheritance, decided AFTER the palette lookup.
- **The frame.** Each frame the game calls `frame_configure(flags, clear
  colour)`, `frame_put`s self-contained primitives (`rv_line`, `rv_polygon` —
  triangle or quad, `rv_sprite`), then `frame_flush()`. The console sorts
  primitives by `rv_primitive::depth` into a hardware **ordering table**
  (larger = nearer / on top; equal depth keeps submission order; out-of-range
  clamps) and renders far-to-near. The optional per-pixel Z test
  (`RV_PIPELINE_BUFFER_CONFIG_TYPE_Z`) layers on top of that ordering — the
  ordering table itself is never off.

Because the console re-orders primitives, there is **no global drawing state**:
texture address, palette address, fill mode and wrap mode all travel inside the
primitive (the PSX texpage/CLUT attributes, generalized).

Hardware numbers (spec values — a later console revision may raise them; the
contract shapes do not change with them):

| Number                   | Value   |
| ------------------------ | ------- |
| Video RAM pool           | 1 MB    |
| Primitive budget / frame | 4096    |
| Ordering-table buckets   | 1024    |
| Max texture size         | 256×256 |

**Dropped on purpose** (PSX features that do not cross into this contract):
drawing area / drawing offset registers (a frame is always the whole 320×240),
polylines (a chain is N line primitives), the texpage state commands, raw VRAM
coordinate addressing (replaced by the allocator), and the mask-bit write
protection.

**Deferred within the video layer**: blending (the semi-transparency modes) and
texture-combine (raw/modulation) flags, VRAM readback, the display/output stage
(`rv_pixel`, 24-bit video), and the PSX fixed-size sprite fast paths
(1×1/8×8/16×16).

**Open — an `src/` decision, not a contract one:** whether the rasterizer
interpolates uv/colour affine (authentic PSX texture warping) or
perspective-correct (what `src/gpu/rasterizer.cpp` does today).

---

## Two directions of the contract

PDK holds **two** interfaces, and they must not be merged — they point opposite ways.

| Interface                | Implemented by | Called by | Meaning                                                     |
| ------------------------ | -------------- | --------- | ----------------------------------------------------------- |
| `rv_pdko` (+ controllers)| the console    | the game  | **the hardware** — GPU, SPU, I/O, drive; unified behind one façade |
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
    │   │ disc.update(dt)       ──► game: pdk_->io()...; … logic           │
    │   │ disc.render()         ──► game: pdk_->video()...;                │
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
- The organizer **vends pointers**, not values: `rv_pdko::video()` returns
  `rv_cv*`. Returning an abstract base by value would slice off the concrete
  implementation — the accessors must hand back a pointer (or reference). The
  pointer is **borrowed**: raw, non-owning.
- A disc holds **only** a `rv_pdko*` / controller pointers. It never owns, copies,
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

### File conventions

- A source file stays under 255 lines.
- PODs/contracts united by one idea may share a file (e.g. `cv/rv_primitives.hpp`
  holds line / polygon / sprite and the `rv_primitive` union); otherwise one type
  per file, as in `ca/`.

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
| `rv_cd`   | **C**ontroller **D**isk (accessor `drive()`) |
| `rv_cm`   | **C**ontroller **M**emory card (accessor `card()`) |

(`rv_` is the project-wide namespace prefix, `namespace rv_3dmppc`.)

Note the disc vs drive distinction: the **disc** is the read-only medium
(`.mppcdisc`); the **drive** (`rv_cd`, `rv_pdko::drive()`) is the console hardware
that reads it. The accessor names the drive, not the disc.

---

## Open decisions

Tracked here so they are chosen deliberately rather than by drift:

1. **`rv_cv` granularity — how thick is the GPU boundary?** *(RESOLVED — the
   low-level way, mirroring audio: video RAM + self-contained primitives over a
   hardware ordering table. See the *Video* section above.)*

2. **Home of the disc-entry interface.** Migrate `rv_Disc` from `src/platform/` into
   `pdk/` so a disc implements one PDK type and calls one PDK type — and rename it to
   fit the PDK scheme, or keep `rv_Disc`.

3. **`rv_cio` open points.**
   - *derived input sources*: the `*_DPAD_*` / `*_MOVE` bits in `rv_isource`
     interpret an analog source past a threshold — arguably binding logic over the
     raw stick value. Kept for Steam Deck coverage; decide whether the device
     surface stays strictly raw.
   - *haptic modelling*: `RV_HAPTIC_EFFECT_WAVEFORM` has no payload yet, and the
     `rv_oheffect::type` tag (currently `1U << n` values) vs a plain enumerator is
     unsettled.

---

## Status

- **`rv_pdko`** — done: virtual destructor + pure-virtual accessors vending
  controllers by pointer (`audio()`, `video()`, `io()`, `drive()`, `card()`).
- **`rv_ca` (audio, low-level)** — surface defined and in progress (sound-RAM
  management + voices). Pitch, reverb, master volume, and ADPCM are deferred.
- **`rv_cio` (input/output)** — surface defined: per-port input snapshot,
  capabilities, mouse, and haptic output (the memory card lives in `rv_cm`).
  Concrete backend in `src/` still pending.
- **`rv_cd` (disc drive)** — surface defined: `asset_open` resolves a
  disc-relative name into a handle, `asset_size` reports the entry's size as a
  sizing hint, `asset_read` copies the whole entry into a buffer the game owns.
  **The drive never allocates the game's data buffer** — the game owns that
  memory, because only the game knows how long the bytes are needed and the RAM
  budget is its to spend. An entry is named by a plain name with no path
  separators, resolved into a handle. The medium is read-only (persistent save is
  the memory card, i.e. `rv_cm`), and no host path ever crosses: the game cannot
  tell a directory from a packed image.
  Deferred: enumeration (the disc builder bakes any listing at build time), ranged
  reads, streaming, and the mapping model (console places the resource in its own
  RAM and lends an address) — the last only pays off once the console owns a real
  fantasy-RAM allocator.
- **`rv_cv` (video, low-level)** — surface defined: video RAM
  (`video_asset_malloc/write/free`; textures + 16-bit palettes with the PSX
  transparency rules), frame submission (`frame_configure` with clear colour and
  the optional Z flag, `frame_put`, `frame_flush`), primitives (line / triangle /
  quad / sprite) sorted by the hardware ordering table. Blending, modulation,
  VRAM readback, the display stage (`rv_pixel`) and the sprite fixed-size fast
  paths are deferred. Concrete backend in `src/` still pending.
- **`rv_cm` (memory card)** — surface defined: 8 KB slots (slot count is console
  configuration, queried via `card_slots()`, default 16 = 128 KB), `card_size` /
  `card_read` / `card_write` / `card_erase`, whole-slot and **atomic** — a failed
  write leaves the old save intact. The card is always inserted; the file-backed
  image is console business. Concrete backend in `src/` pending (today's
  `core/savecard.*` becomes it).
- The console (`src/`) and the reference disc (`mppcdiscs/solidmaid/`) still use the
  older in-binary path (`rv_Disc` + `rv_DiscServices` + a raw framebuffer). Moving
  them behind `rv_pdko`, and rewriting the game's high-level audio onto the
  low-level `rv_ca` and its rendering onto `rv_cv`, is the next step.
