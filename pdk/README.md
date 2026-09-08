# Phantasy Development Kit (PDK)

The **devkit** for the **3DMPPC** — *3D Math Prime Phantasy Console*, a fantasy
console in the spirit of the Sony PlayStation (PSX).

PDK is the **contract** between the console and the games that run on it. It is
neither the console nor a game: it is the third thing both sides depend on — the
headers a game is written against, exactly like a real console ships a devkit of
headers rather than the firmware source.

> Scope: PDK defines *the boundary*. How a disc is packaged and loaded at runtime
> (`.mppcdisc`, `dlopen`, extract-to-temp, ELF-note version handshake) is a separate
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
| `rv_pdko`  | `rv_pdko.h`        | organizer / façade                  | `rv_pdko_ca()`, `rv_pdko_cv()`, `rv_pdko_cio()`, `rv_pdko_cd()`, `rv_pdko_cm()`, `rv_pdko_cl()` — free functions, opaque handle in, controller pointer out |
| `rv_ca`    | `ca/rv_ca.h`       | **C**ontroller **A**udio (SPU)      | low-level: `sound_asset_malloc`/`sound_asset_write`/`sound_asset_free`, `voice_setup`/`voice_play`/`voice_stop`/`voice_status` |
| `rv_cv`    | `cv/rv_cv.h`       | **C**ontroller **V**ideo (GPU)      | low-level: `video_asset_malloc`/`video_asset_write`/`video_asset_free` (textures + palettes), `frame_configure`/`frame_put`/`frame_flush` (primitives, sorted by the hardware ordering table) |
| `rv_cio`   | `cio/rv_cio.h`     | **C**ontroller **I**nput/**O**utput | input snapshot (`iport_state`) + capabilities (`iport_abilities`) + mouse (`imouse`) + haptic out (`ohaptic`) |
| `rv_cd`    | `cd/rv_cd.h`       | **C**ontroller **D**isk (drive)     | `asset_open` (name → handle) / `asset_size` / `asset_read` into the game's buffer |
| `rv_cm`    | `cm/rv_cm.h`       | **C**ontroller **M**emory card      | persistent save slots: `card_slots` (count from console config) / `card_size` / `card_read` / `card_write` (atomic) / `card_erase` |
| `rv_cl`    | `cl/rv_cl.h`       | **C**ontroller **L**ua (script machine) | chunk lifecycle: `script_load`/`script_free`/`script_entry`; a shared value stack: `stack_push_*`/`stack_drop`/`stack_count`/`value_*`; one call primitive, `script_call` |

Shared vocabulary lives next to the controllers: the audio POD is in `ca/`
(`rv_sample`, `rv_voice_conf`, `rv_loop`), the I/O POD is in `cio/` (`rv_isource`,
`rv_istate`, `rv_iaxes`, `rv_imotion`, `rv_imouse`, `rv_ohaptic`), the video POD
is in `cv/` (`rv_color`, `rv_uv`, `rv_vertex`, `rv_texture`, and the primitive
family in `rv_primitives.hpp`), and the cross-controller error enum is
`rv_err.h` (see *Error convention*). `cl/` carries no separate POD tree —
its one shared type, `rv_cl_type` (nil/boolean/number/string/function/table/
other), only tags a value already sitting on the script machine's own stack.

Subsystem split, PSX-faithful:

- **`rv_cd`** is the **drive** — the hardware that *reads* the read-only optical
  **disc** (the `.mppcdisc` medium). The accessor is `rv_pdko_cd()`: you talk
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
  accessor is `rv_pdko_cm()`. The medium is a set of equally-sized **slots**;
  count and size are implementation values, queried at boot (`card_slots()` /
  `card_slot_size()`) and validated against the save blob the game was built
  with. The card is always inserted; persistence (the file-backed image)
  is the console's concern, never an operation the game invokes. Writes replace
  a slot whole and are **atomic**: a failed write leaves the old save intact.
- **`rv_cl`** is the **script machine** — a Lua VM a disc may choose to load
  bytecode into and drive. It is the one controller a disc can opt out of
  entirely: `[budget.pccl]` in the manifest (`script_memory_size`,
  `script_entry`) is what brings a VM into existence at all, on the same terms
  `[budget.pcca]`/`[budget.pccv]` size sound and video RAM, and
  `rv_pdko_cl()` answers `nullptr` — not a live-looking handle that always
  fails — for a disc that never asked for one
  (`src/rv_pconsole/rv_pconsole.cpp`). The console itself never executes a
  line of Lua and never dispatches through `rv_cl`: it sizes and hands out the
  machine exactly as it hands out VRAM, and the DISC is the one that decides
  whether to drive it (see "Three paths across the boundary").

### Class realization — opaque in `pdk/`, concrete at the edges

`rv_pdko` and its six controllers are **opaque C11 structs**: `pdk/` declares
`typedef struct rv_ca rv_ca;` and a page of function prototypes, never a
definition and never a base class. There is nothing to inherit — a disc holds
a pointer it can never dereference itself, and every operation on it is a
free function taking that pointer as its first argument
(`rv_cv_frame_put(cv, prim)`, not `cv->frame_put(prim)`). This is the same
plain-C11 shape `rv_de` uses for the opposite direction (a POD table of
function pointers, see "Three paths across the boundary" below) — the whole
contract is one ABI, not two:

```
  ╔══════════════════════════ pdk/  (PHANTASY DEV KIT) ══════════════════════════╗
  ║  OPAQUE C11 types + free functions. No inheritance, nothing virtual.         ║
  ║                                                                              ║
  ║                     rv_pdko *o;  // opaque handle, one per console          ║
  ║        rv_pdko_ca(o)->rv_ca*   rv_pdko_cd(o)->rv_cd*   rv_pdko_cm(o)->rv_cm*║
  ║        rv_pdko_cio(o)->rv_cio* rv_pdko_cv(o)->rv_cv*   rv_pdko_cl(o)->rv_cl*║
  ║                                                        (rv_cl* may be NULL) ║
  ║                                                                              ║
  ║  ┌───────┐   ┌───────┐   ┌───────┐   ┌───────┐   ┌───────┐   ┌───────┐      ║
  ║  │ rv_ca │   │ rv_cv │   │ rv_cio│   │ rv_cd │   │ rv_cm │   │ rv_cl │      ║
  ║  │malloc │   │frame_*│   │iport_*│   │asset_*│   │card_* │   │script_*      ║
  ║  │voice_*│   └───────┘   └───────┘   └───────┘   └───────┘   │stack_*      ║
  ║  └───────┘                                                   │value_*│     ║
  ║                                                               └───────┘     ║
  ║   POD: audio in ca/, i/o in cio/, video in cv/ · rv_err (shared)             ║
  ║   + the disc-entry table the game builds (see "Three paths")                ║
  ╚══════════════════════════════════════════════════════════════════════════════╝
                      △  casts the handle                    △  builds the table
        ┌─────────────┴─────────────────┐        ┌───────────┴────────────────┐
        │  src/  (THE CONSOLE)          │        │  mppcdiscs/<game>/         │
        │                               │        │                            │
        │  class rv_pconsole            │        │  class rv_dmain            │
        │   owns the loop + six members:│        │   (no base class — its      │
        │   ├ (video)    : rv_pccv      │        │    methods just match       │
        │   │   └ rv_Rasterizer,        │        │    what RV_MPPC_DISC_       │
        │   │     rv_Framebuffer  (priv)│        │    ENTRY_DEF thunks call)   │
        │   ├ (audio)    : rv_pcca      │        │    rv_pdko* pdk_;          │
        │   ├ (input)    : rv_pccio     │        │    // disc_initialize:     │
        │   ├ (drive)    : rv_pccd      │        │    //   pdk_ = pdk         │
        │   ├ (card)     : rv_pccm      │        │    // frame_render:        │
        │   └ (script)   : rv_pccl      │        │    //   rv_pdko_cv(pdk_)   │
        │  free rv_pdko_cX(o) casts o   │        │    //   rv_cv_frame_put(…) │
        │  to rv_pconsole* and returns  │        │                            │
        │  &cX_ as the opaque type      │        │                            │
        └───────────────────────────────┘        └────────────────────────────┘
         concrete backends are PRIVATE to          a game sees only rv_pdko and
         src/, never exposed, never in pdk/         its controllers — no src/ type
```

---

## Audio — two layers (low-level now, sequencer later)

Audio deliberately follows the PSX split of a low-level sound-chip library and a
high-level sequencer built on top of it:

- **Low-level — `ca/rv_ca.h` (the SPU).** The realized layer. The game manages a
  private pool of **virtual sound RAM** — the console's own fixed budget, not the
  host's memory (`sound_asset_malloc` → `sound_asset_write` sample
  → `sound_asset_free`) and drives a
  fixed set of **voices** (`voice_setup` a config, then `voice_play` / `voice_stop`
  / `voice_status` by voice bitmask). This is the hardware boundary the console
  implements. Sample data crosses as `rv_sample` (POD), voice settings as
  `rv_voice_conf` (POD), loop behaviour as `rv_loop`.

**Deferred within the audio layer** (tracked so they are conscious omissions, not
oversights): per-voice **pitch/playback rate**, **reverb**, **master volume**, and
**ADPCM** sample encoding (raw PCM for now).

### Error convention (`rv_err.h`)

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

**`enum` vs `#define`.** The contract states a constant one of two ways, and the
choice is not taste:

* **`enum`** when the name is used as a *type* — something is declared with it.
  `rv_err`, `rv_texfmt`, `rv_texture_mapping_type`, `rv_loop`. A field holding one
  of these carries the enum type itself (`rv_texture::format`), so no cast is
  needed at the point of use.
* **`#define`** when the value only ever *lives in a field of fixed width* —
  `uint32_t type`, `uint64_t buttons`, the `config` mask of
  `rv_cv_frame_configure`. The enum name would then appear nowhere but a comment,
  and C has no way to widen an enum past `int` anyway: `RV_ISOURCE_GYRO_ROLL` is
  bit 52 and would silently become `0`.

Every such `#define` states its bit index in a trailing comment (`// bit 7`),
because a hex mask alone does not say which bit it is — the `1ULL << 7` form did.
Values that are a sequence rather than a mask (`RV_PRIMITIVE_POLYGON`) carry no
bit comment, and that absence is the signal that they are not combinable.

These `#define`s sit OUTSIDE the `RV_CDEF_BEGIN` / `RV_CDEF_END` markers, because
a preprocessor directive is exactly what `ffi.cdef` cannot read. A script side
that needs the constants gets them generated as a table, not through `cdef` —
this is now built, not hypothetical: `cmake/rv_cdef_gen.cmake` scrapes exactly
these `#define`s into a second generated string, `rv_pdk_consts`, compiled
alongside the sliced `RV_CDEF_BEGIN`/`RV_CDEF_END` regions (`rv_pdk_cdef`) into
`build/rv_pdk_cdef.cpp`. See "Three paths across the boundary" for why there
are two outputs and not one.

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

- **Video RAM** — virtual, on the same terms as sound RAM: a fixed pool the
  console owns and runs out of. The game reserves a region
  (`video_asset_malloc`), uploads texture or palette data (`video_asset_write` +
  `rv_texture`), and keeps only
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

**Hardware geometry is implementation-defined — the PDK carries no numbers.**
Whatever a concrete machine answers is a **virtual** budget it imposes on
itself, not a measurement of the host it runs on; the console is expected to
hold the line it names, which is why the pool calls can answer `RV_ERR_NOMEM` at
all. The devkit only gives a game the means to ASK: `screen_width/height`,
`texture_max_width/height`, `video_memory_size`, `frame_capacity` (video);
`voice_count`, `sound_memory_size` (audio); `card_slots`, `card_slot_size`
(memory card); `iport_count`, `iport_abilities` (input). A disc queries these in
`disc_initialize`, validates the assumptions its assets were built against, and
returns a negative `rv_err` on a mismatch — the console then refuses to run it.
The reference console's defaults live in `docs/platform/specs.md`.

One number stays hidden on purpose: the ordering-table bucket count. The game
hands depth VALUES, never bucket indices — how finely the console quantizes is
its own business.

**Dropped on purpose** (PSX features that do not cross into this contract):
drawing area / drawing offset registers (a frame is always the whole 320×240),
polylines (a chain is N line primitives), the texpage state commands, raw VRAM
coordinate addressing (replaced by the allocator), and the mask-bit write
protection.

**Deferred within the video layer**: blending (the semi-transparency modes) and
texture-combine (raw/modulation) flags, VRAM readback, the display/output stage
(24-bit video), and the PSX fixed-size sprite fast paths
(1×1/8×8/16×16).

**Open — an `src/` decision, not a contract one:** whether the rasterizer
interpolates uv/colour affine (authentic PSX texture warping) or
perspective-correct (what `src/gpu/rasterizer.cpp` does today).

---

## Three paths across the boundary

PDK carries traffic across the console/game boundary three ways, and they must
not be conflated — two point opposite directions, and the third reaches
through one of those two verbatim, from a different caller.

| Path              | Crossed via                                                        | Meaning                                                                    |
| ----------------- | ------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| console → disc    | `rv_de` — a POD table of function pointers                         | **the cartridge's pins** — the `disc_*`/`frame_*` hooks the console drives |
| disc → console    | the exported `rv_*` free functions (`rv_pdko_*`, `rv_ca_*`, `rv_cv_*`, `rv_cl_*`, …) | **the hardware** — GPU, SPU, I/O, drive, card, script machine; one façade  |
| script → console  | **the same exported functions, again** — reached from Lua via `ffi.C` | the disc's own choice to run code on the script machine; not a fourth interface |

### console → disc: `rv_de`

`rv_de` (`pdk/de/rv_de.h`) is a **plain struct of function pointers**, not a
C++ base class — nothing about it is virtual, and a disc does not inherit
from it:

```c
struct rv_de {
    void *self;
    int64_t (*disc_initialize)(void *self, rv_pdko *pdk);
    void (*frame_update)(void *self, float dt);
    void (*frame_render)(void *self);
    int (*disc_release)(void *self);
    void (*disc_shutdown)(void *self);
    const char *(*disc_title)(void *self);
};
```

By convention a disc still writes its logic as an ordinary C++ class named
`rv_dmain` — the loader-facing name is fixed, like `main()` for programs — but
`rv_dmain` implements no interface at all; it merely happens to have methods
of matching names. `RV_MPPC_DISC_ENTRY_DEF(rv_dmain)` (`pdk/de/rv_dv.h`)
generates the six thunks that turn those methods into the function pointers
above, plus the `create`/`destroy` pair a `.mppcdisc` exports under fixed
names for the loader to `dlsym`. Thunks, not inheritance, because a C++
vtable is not a stable ABI across a `dlopen` boundary — a flat
function-pointer struct is. `rv_Disc` + `rv_DiscServices` from the old
`src/platform/disc.hpp` are gone; nothing in `src/` uses them any more.

- `disc_initialize(self, rv_pdko*)` — once, before the first frame: the disc
  stashes the facade, queries capabilities, loads assets, reads its save.
  Returns `>= 0`, or a negative `rv_err` — the console then refuses to run
  the disc (`rv_err` starts here).
- `frame_update(self, dt)` — one frame of simulation; input is pulled through
  `rv_cio_*`. Do one frame's worth of work and RETURN — the console cannot
  preempt a hook.
- `frame_render(self)` — build the frame (`rv_cv_frame_configure` →
  `rv_cv_frame_put`s) and end it with `rv_cv_frame_flush`. Skipped in
  headless runs.
- `disc_release(self)` — a QUERY, polled every frame: "does the disc ask the
  console to power off?" It releases nothing itself.
- `disc_shutdown(self)` — the teardown hook: BUILT, not deferred. For the
  built-in disc it runs once after the frame loop exits; for a loaded
  `.mppcdisc` it is the first link of the loader's own teardown chain,
  `disc_shutdown → mppc_disc_destroy → dlclose → unlink`
  (`src/rv_pconsole/rv_pcloader.cpp`) — always in that order, so a disc still
  has every controller while it tears down, and the code behind it is never
  unmapped out from under a call in flight.
- `disc_title(self)` — name for the window title / logs (a string literal).

### disc → console and script → console: the exported `rv_*` functions

`rv_pdko` and its six controllers are opaque C11 structs; every operation on
one is a free function taking the handle first (`rv_pdko_cv(o)`,
`rv_cv_frame_put(cv, prim)`). Those exported functions ARE the entire
disc → console direction — there is no second, wrapped interface sitting
next to them for a game to call instead.

A Lua script reaches the identical functions, not a copy and not a
hand-written binding layer: the script machine (`rv_cl`, see "Architecture"
above) hands a chunk `ffi.C.rv_cv_frame_put`, which LuaJIT resolves by
`dlsym`-ing the running executable — the same lookup a `.mppcdisc`'s own
`dlopen`ed code performs to reach the console. This works with **no wrapper
layer, deliberately**, for exactly two reasons: the contract is plain C11
(`extern "C"`, no name mangling, no calling-convention ambiguity), and the
console target sets `ENABLE_EXPORTS ON` (`-rdynamic`) —

> "A disc looks the contract's functions up in the executable through dlsym,
> and the LuaJIT FFI will look for them in the same place. Without
> ENABLE_EXPORTS (which is -rdynamic) the executable's dynamic symbol table
> is empty and neither side finds anything." — `CMakeLists.txt`

— so those symbols actually sit in the executable's dynamic symbol table for
`dlsym`/`ffi.C` to find. Take either property away and the third path stops
existing; neither is incidental.

Because of this, **the console never learns that Lua exists as an execution
path.** It knows `rv_cl` only as a controller it sizes and hands out, the
same way it sizes and hands out `rv_cv`'s video RAM: `rv_pconsole::cl()`
(`src/rv_pconsole/rv_pconsole.cpp`) just returns a pointer or `nullptr`, and
the frame loop (`rv_pconsole::disc_run`) calls `disc->frame_update`/
`frame_render` identically whether or not that disc forwards the call into a
Lua chunk. The DISC drives the machine: every hook in
`mppcdiscs/example-lua/src/example-lua.cpp` is one `rv_cl_script_call`
forwarding into `scripts/example-lua.lua`, while the console's own frame
loop (`rv_pconsole.cpp`) never mentions `rv_cl` or Lua at all.

**Why `ffi.cdef` cannot just read the header.** LuaJIT's `ffi.cdef()` takes a
string of C declarations — it is not a preprocessor: it cannot follow
`#include` and it cannot expand `#define`. `cmake/rv_cdef_gen.cmake` exists
because of exactly that gap. It slices every header's
`/* RV_CDEF_BEGIN */ ... /* RV_CDEF_END */` region — the declarable C, types
and prototypes — into one generated string, and separately scrapes every
bit-mask/`#define` constant (see "`enum` vs `#define`" above) into a second
string of literal Lua source (`return { NAME = value, ... }`). Both land in
`build/rv_pdk_cdef.cpp` as `rv_pdk_cdef` / `rv_pdk_consts`, compiled straight
into the console executable: **two outputs, not one**, because the two
things `ffi.cdef` cannot read (`#include`, `#define`) need two different
escape hatches.

**What a script actually sees: exactly one global, `pdk`.** `rv_pccl`'s
constructor (`src/rv_pconsole/cl/rv_pccl.cpp`) opens only
`base`/`string`/`math`/`table` and LuaJIT's `ffi` — deliberately never `io`,
`os`, or `package`/`require` — feeds it `rv_pdk_cdef` through `ffi.cdef`,
turns `rv_pdk_consts` into a table, and installs that table as `_G.pdk` once
`RV_PCCL_PDK_BOOTSTRAP_SRC` has wired it up: `pdk.cast`/`pdk.new` are
`ffi.cast`/`ffi.new` directly, and every other key resolves lazily through a
metatable `__index` that tries `ffi.C["rv_"..k]` then `ffi.C["RV_"..k]` and
memoizes whichever one hits — so a script writes `pdk.cv_frame_put(cv, prim)`
or `pdk.TEXWRAP_CLAMP` and never says `ffi` itself, which is never reachable
any other way.

This is **hygiene, not a sandbox.** `pdk.cast` and `pdk.new` ARE
`ffi.cast`/`ffi.new`, so a script can build a pointer from a bare integer and
read or write arbitrary process memory — tested and confirmed. Withholding
`io`/`os` keeps a disc from reaching the *world* except through its
controllers; it does nothing to stop a disc reaching anywhere *inside* the
process it already runs in. Treat the library policy as accident-prevention,
never as isolation a reader could rely on.

---

## Runtime — handshake and a frame

```
  rv_pconsole (src/) — the built-in disc runs statically linked; a .mppcdisc
    │        runs via dlopen (packaging/loading mechanics are a separate
    │        concern — see ../docs/platform/disc-loading.md)
    │
    │ 1. build the CONCRETE console (it constructs its own six controllers)
    ├──────────►  rv_pconsole console;   // owns ca_/cd_/cio_/cl_/cm_/cv_
    │
    │ 2. obtain the disc object: rv_de* disc;
    │                 // a POD table of function pointers + self; the game's
    │                 // class is named rv_dmain by convention (not a base)
    │
    │ 3. disc_run(disc) — HANDSHAKE: the console hands ITSELF over as rv_pdko*
    ├──────────►  disc->disc_initialize(disc->self, this);  ──► disc stashes rv_pdko*
    │                 (negative rv_err → the console refuses to run the disc)
    │
    │ 4. frame loop (owned by the console — it just lives by it):
    │   ┌──────────────────────────────────────────────────────────────────┐
    │   │ console: poll SDL events             // → iport_state snapshots   │
    │   │ disc->frame_update(self, dt)  ──► game: rv_pdko_cio(pdk_), …      │
    │   │ disc->frame_render(self)      ──► game: rv_cv_frame_put/flush(…)  │
    │   │ console: present                     // framebuffer → window      │
    │   └──────────────────────────────────────────────────────────────────┘
    │        ▲ console calls the disc (rv_de)   ▼ disc calls the exported
    │                                              rv_* functions (rv_pdko_*)
    │            two opposite arrows — the whole thing hangs on them. A
    │            script reaches the SAME lower arrow through ffi.C — see
    │            "Three paths across the boundary"
    │
    │ 5. teardown: disc_release() == true → leave loop, disc->disc_shutdown(self)
    └──────────►  (a loaded disc's teardown then continues into the loader's
                   own chain — see ../docs/platform/disc-loading.md)
```

---

## Ownership and lifetime

- The controllers (`rv_ca` / `rv_cv` / `rv_cio` / `rv_cd` / `rv_cm` / `rv_cl`)
  are **owned by the concrete console** and live as long as it does — except
  `cl_`, which is deliberately declared LAST among the console's members
  (`src/rv_pconsole/rv_pconsole.hpp`), out of alphabetical order, so it is
  destroyed FIRST: a Lua finalizer can call back into `rv_cv_*`/`rv_ca_*`
  through `ffi.C` while the machine shuts down, and `lua_close()` must run
  before any controller it might still reach is gone.
- The organizer **vends pointers**, not values: `rv_pdko_cv(o)` returns
  `rv_cv*`. `rv_pdko` and its controllers are opaque C11 structs with no
  definition a caller could hold by value even if it wanted to — the
  accessors are free functions that `reinterpret_cast` the handle back to the
  concrete `rv_pconsole` and return the address of a member, cast to the
  opaque type. The pointer is **borrowed**: raw, non-owning.
- A disc holds **only** a `rv_pdko*` / controller pointers. It never owns, copies,
  or destroys them. `rv_pdko_cl()` is the one accessor that may hand back
  `nullptr` instead of a pointer — a disc that declared no `[budget.pccl]`
  gets no script machine at all.

---

## Separation — enforced by the build, not by discipline

PDK is a header-only interface target. The include paths make the boundary
physically impassable:

```cmake
add_library(3dmppc_pdk INTERFACE)
target_include_directories(3dmppc_pdk INTERFACE ${CMAKE_SOURCE_DIR}/pdk)  # ONLY pdk/

# console: sees pdk AND its own internals
target_link_libraries(3dmppc PRIVATE 3dmppc_pdk)
target_include_directories(3dmppc PRIVATE src)

# a game: sees pdk ONLY. It is not given a path into src/, so it physically
# cannot write #include "gpu/rasterizer.hpp" — the compiler refuses.
target_link_libraries(<game> PRIVATE 3dmppc_pdk)
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
| `rv_cd`   | **C**ontroller **D**isk (accessor `rv_pdko_cd()`) |
| `rv_cm`   | **C**ontroller **M**emory card (accessor `rv_pdko_cm()`) |
| `rv_cl`   | **C**ontroller **L**ua — the script machine (accessor `rv_pdko_cl()`, may be `NULL`) |
| `rv_de` | console → disc: a POD table of hook function pointers (`disc_*` / `frame_*`) a `.mppcdisc` builds |
| `rv_dmain` | convention: the name of the plain C++ class a disc writes its hooks in; not a base of `rv_de` |
| `rv_pconsole` | the concrete console in `src/` — the object behind the `rv_pdko*` handle, owns the loop (`rv_pconsole::disc_run`) |
| `rv_mppc_disc_create_fn` / `_destroy_fn` | the two fixed-name symbols (`pdk/de/rv_dv.h`) a `.mppcdisc` exports for the loader to `dlsym` |

(`rv_` is the project-wide type prefix. The contract itself is plain C11 —
`extern "C"`, no C++ namespace of its own; the console that implements it
lives in `rv_3dmppc`, each game in its own.)

Note the disc vs drive distinction: the **disc** is the read-only medium
(`.mppcdisc`); the **drive** (`rv_cd`, accessor `rv_pdko_cd()`) is the console
hardware that reads it. The accessor names the drive, not the disc.

---

## Open decisions

Tracked here so they are chosen deliberately rather than by drift:

1. **`rv_cv` granularity — how thick is the GPU boundary?** *(RESOLVED — the
   low-level way, mirroring audio: video RAM + self-contained primitives over a
   hardware ordering table. See the *Video* section above.)*

2. **Home of the disc-entry interface.** *(RESOLVED — `rv_de` lives in
   `pdk/de/rv_de.h` as a POD table of function pointers (`self` + six hooks:
   `disc_initialize` / `frame_update` / `frame_render` / `disc_release` /
   `disc_shutdown` / `disc_title`); a disc writes an ordinary class named
   `rv_dmain`, glued to the table by `RV_MPPC_DISC_ENTRY_DEF`'s thunks — no
   inheritance crosses the boundary. See "Three paths across the boundary".
   `rv_Disc` + `rv_DiscServices` are gone from `src/` — the migration is
   done, not merely started.)*

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

- **`rv_pdko`** — done: an opaque C11 handle, not a C++ base class, vending
  its six controllers by pointer through free functions (`rv_pdko_ca()`,
  `rv_pdko_cv()`, `rv_pdko_cio()`, `rv_pdko_cd()`, `rv_pdko_cm()`,
  `rv_pdko_cl()`).
- **`rv_ca` (audio, low-level)** — surface defined and in progress (sound-RAM
  management + voices + geometry queries `voice_count`/`sound_memory_size`).
  Pitch, reverb, master volume, and ADPCM are deferred.
- **`rv_cio` (input/output)** — surface defined: per-port input snapshot,
  capabilities, mouse, and haptic output (the memory card lives in `rv_cm`).
  Concrete backend built in `src/rv_pconsole/cio/rv_pccio.cpp`.
- **`rv_cd` (disc drive)** — surface defined: `asset_open` resolves a
  disc-relative name into a handle, `asset_size` reports the entry's size as a
  sizing hint, `asset_read` copies the whole entry into a buffer the game owns.
  **The drive never allocates the game's data buffer** — the game owns that
  memory, because only the game knows how long the bytes are needed and the RAM
  budget is its to spend — that budget being virtual and, unlike video and sound
  RAM, not enforced by anything yet. An entry is named by a plain name with no
  path separators, resolved into a handle. The medium is read-only (persistent
  save is the memory card, i.e. `rv_cm`), and no host path ever crosses: the
  game cannot tell a directory from a packed image.
  Deferred: enumeration (the disc builder bakes any listing at build time), ranged
  reads, streaming, and the mapping model (console places the resource in its own
  RAM and lends an address) — the last only pays off once the console owns a real
  fantasy-RAM allocator.
- **`rv_cv` (video, low-level)** — surface defined: geometry queries
  (`screen_*`, `texture_max_*`, `video_memory_size`, `frame_capacity`), video
  RAM (`video_asset_malloc/write/free`; textures + 16-bit palettes with the PSX
  transparency rules), frame submission (`frame_configure` with clear colour and
  the optional Z flag, `frame_put`, `frame_flush`), primitives (line / triangle /
  quad / sprite) sorted by the hardware ordering table. Blending, modulation,
  VRAM readback, the display stage and the sprite fixed-size fast
  paths are deferred. Concrete backend built in `src/rv_pconsole/cv/rv_pccv.cpp`.
- **`rv_cm` (memory card)** — surface defined: equally-sized slots (count and
  size queried via `card_slots()` / `card_slot_size()`), `card_size` /
  `card_read` / `card_write` / `card_erase`, whole-slot and **atomic** — a failed
  write leaves the old save intact. The card is always inserted; the file-backed
  image is console business. Concrete backend built in
  `src/rv_pconsole/cm/rv_pccm.cpp`.
- **`rv_cl` (script machine, Lua)** — surface defined and built: chunk
  lifecycle (`script_load`/`script_free`/`script_entry`), a shared value
  stack (`stack_push_*`/`stack_drop`/`stack_count`/`value_*`), and one call
  primitive (`script_call`) with a documented stack discipline. Concrete
  backend `src/rv_pconsole/cl/rv_pccl.cpp` wraps LuaJIT: a private
  sound-RAM-style allocator caps a script's memory at `[budget.pccl]
  script_memory_size`, only `base`/`string`/`math`/`table`/`ffi` are opened
  (never `io`/`os`/`package`), and the console's own PDK surface reaches the
  VM as the single global `pdk`, generated from the real headers by
  `cmake/rv_cdef_gen.cmake` (see "Three paths across the boundary"). The
  controller is OPTIONAL per disc — no `[budget.pccl]` means no VM and
  `rv_pdko_cl()` answers `nullptr` — and its `pdk.cast`/`pdk.new` are raw
  `ffi.cast`/`ffi.new`: hygiene against an accidental `io`/`os` reach, not a
  security sandbox. Deferred: nothing named yet — this is the newest
  controller and no gaps have surfaced.
- **`rv_de` (disc entry)** — surface defined and written (`de/rv_de.h`): a
  POD table of six function pointers, `disc_initialize` (the game's only
  fallible hook, returns `rv_err`), `frame_update` / `frame_render` (one
  frame's worth each; render ends with `rv_cv_frame_flush`), `disc_release`
  (power-off query), `disc_shutdown` (the teardown hook — built, wired into
  the loader's `disc_shutdown → mppc_disc_destroy → dlclose → unlink` chain),
  `disc_title`. A disc writes its logic in a class named `rv_dmain`, bound to
  the table by `RV_MPPC_DISC_ENTRY_DEF`'s generated thunks rather than
  inheritance. `disc_initialize` is where the disc queries the hardware
  geometry and validates its baked assumptions.
- The migration off the old in-binary path is DONE: `rv_Disc` and
  `rv_DiscServices` are gone from `src/` — every disc, built-in or loaded,
  now runs behind `rv_pdko` and the six controllers above.
