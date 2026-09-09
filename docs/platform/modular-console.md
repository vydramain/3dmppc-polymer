# Modular console — swapping implementations behind the contract

**Status: draft. Not in the code yet.** The document records the decisions made
about how a subsystem's implementation is replaced without touching the disc,
the PDK, or `rv_pconsole`. The contract a disc sees is described in
[`disc-loading.md`](disc-loading.md); this is what should sit behind it.

---

## The principle

The PDK says **what the machine can do**. The internal interface says **who
implements it right now**.

A disc already sees only opaque handles — `rv_ca *`, `rv_cv *`, `rv_cl *` — and
free C functions. It cannot name a C++ class and cannot tell one implementation
from another. The same separation is missing *inside* the console: today each
contract function casts straight to the one concrete class that implements it,
so a second implementation cannot exist in the process at all.

Replacing an implementation must not reach the disc, the PDK or the composition
root. Turning a subsystem off must not scatter `if (disabled)` through the call
paths.

---

## Layers

```
disc
 |  calls free C functions
 v
rv_ca / rv_cv / rv_cio / rv_cl / rv_cd / rv_cm     contract, opaque C, pdk/
 |  reinterpret_cast — exactly one definition per contract
 v
rv_pcca / rv_pccv / rv_pccio / rv_pccl / ...       base, purely virtual
 |  virtual call
 v
rv_pcca_sdl3   rv_pcca_null   rv_pcca_rubix        implementations, peers
 |  borrow the platform by reference
 v
rv_pchost_sdl3                                     seventh slot: window, events, devices
```

The base keeps the name the concrete class has today. `rv_pcca` stops being an
implementation and becomes the interface; the implementation moves down and
gains a suffix.

---

## One slot in files

| File | Holds |
| ---- | ----- |
| `ca/rv_pcca.hpp` | the base: the contract's nine pure virtuals, plus the console-side methods, plus a virtual destructor |
| `ca/rv_pcca.cpp` | **only** the `extern "C"` block — today's `rv_pcca.cpp:304-347`, unchanged |
| `ca/rv_pcca_sdl3.*` | today's `rv_pcca.cpp:1-300`; the fields `conf_`, `host_`, `sram_`, `mixer_` |
| `ca/rv_pcca_null.*` | today's nine `!sounding_` branches, as whole method bodies |

No implementation contains an `extern "C"` block and none performs the reverse
cast. What leaves `rv_pcca.cpp` is the implementation, not the dispatcher: the
dispatcher already casts to `rv_pcca`, and once `rv_pcca` is the base the very
same line starts landing on a virtual call.

---

## Rules

1. `rv_ca *` is the address of the **base**. `reinterpret_cast` is mandatory,
   not a preference: `rv_ca` is an incomplete type, so `static_cast` from it
   cannot compile.
2. Slots are **named `std::unique_ptr` fields**, never a container. Members are
   destroyed in reverse declaration order whether they are values or unique
   pointers, so the existing rule keeps holding: `cl_` is declared last and
   therefore dies first, because a Lua finaliser may call back through the FFI
   into a controller that must still exist.
3. "Off" is an implementation, not a flag. There is no `if (subsystem disabled)`
   on any call path.
4. A `_null` impersonates hardware where the contract allows it and refuses
   honestly where it does not (see below).
5. A base carries two groups of methods: **contract** methods, reached from the
   `extern "C"` block, and **console-side** methods, reached only by the
   console. Console-side methods never appear in the PDK.
6. Only `rv_pchost_sdl3` and slots suffixed `_sdl3` know SDL exists.
7. No common ancestor over the six slots. It would buy nothing and would invite
   a container, which rule 2 forbids.

---

## Null implementations

`ca` can lie: `voice_count()` reports the declared number, `sound_asset_malloc`
returns a fixed valid fake address, every action answers `RV_OK`, and
`voice_status` reports no voice busy. A no-op console still describes the
hardware shape a real one would have had. That behaviour is already written —
it is the nine branches inside today's `rv_pcca`.

`cl` cannot lie. `rv_cl_script_call` promises `retc` results on the stack on
success and `rv_cl_stack_count` counts them (`pdk/cl/rv_cl.h:83-96`); a machine
with no VM cannot put them there, so a fake `RV_OK` would corrupt the caller's
stack accounting. `cl_null` therefore returns `RV_ERR_INVAL`, which is the
correct Null Object for this contract rather than a departure from it.

The difference is dictated by the contract, not by taste: `ca` has no return
channel, `cl` has one with an invariant.

`rv_pdko_cl` stops returning `nullptr`. A disc that declared scripts and got
`cl_null` still refuses to start, one line later, when `rv_cl_script_entry`
answers negative — the path the lua example disc already takes.

---

## Selecting an implementation

```
--mode=sdl3              a preset: one choice per slot
--mode_cv=null|sdl3|vk   a per-slot override
```

`--headless`, `--no-audio` and the never-added `--no-input` collapse into
`--mode_<slot>=null`. `--mute` stays a flag: it is a setting of `ca_sdl3`, a
quieter machine rather than a different one.

Order of application: built-in preset -> preset file -> `--mode` ->
`--mode_<slot>`.

Today's vocabulary is `ca`, `cv`, `cio` in `{sdl3, null}` and `cl` in
`{luajit, null}`. The preset named `sdl3` is
`{ca: sdl3, cv: sdl3, cio: sdl3, cl: luajit}` — a preset's name and a slot's
value do not always coincide.

The slot choice is consumed by the factory only. It must not be added to
`rv_pcca_conf` or `rv_pccv_conf`, because those structs are handed to the
implementation itself, and an implementation reading its own identity is
meaningless.

### The preset file

User presets live in a file next to the executable, in the manifest's own
syntax:

```
[mode.sdl3]
ca  = sdl3
cv  = sdl3
cio = sdl3
cl  = luajit

[mode.silent]
ca  = null
```

An absent slot falls back to the built-in preset, the same rule the manifest
already uses for an absent budget key. The built-in table is compiled in and
mandatory: the console is firmware and must boot with no file at all.

The manifest front end is reused as is. The lexer, the parser and the tree are
schema-independent by construction — the parser "knows nothing about which
sections exist" — and only the semantic stage binds to a schema table. A preset
file needs its own schema table and binder, nothing more.

### The one exception

The budget states the machine's numbers; the launch configuration picks the
implementations. Lua is the single exception: an absent `[budget.pccl]` selects
`cl_null` regardless of the preset, and `--mode_cl=luajit` will not raise a VM
for a disc that carries no scripts. The reverse is allowed — `--mode_cl=null`
boots the console and lets the disc refuse for itself.

---

## The host slot

`rv_pchost` stays a separate subsystem rather than dissolving into the slots.
`SDL_PollEvent` drains one queue that carries window events, gamepad arrival and
mouse motion together; two slots each polling it would steal each other's
events.

The line is drawn between owning the platform and owning a graphics API on top
of it:

| Stays in the host | Moves into `rv_pccv_sdl3` |
| ----------------- | ------------------------- |
| `SDL_Window`, `open()`, `display_bounds()`, `pump()`, `power_off()` | `renderer_`, `texture_`, `present()`, `presenting()`, `dump_frame()`, `dump_last_frame()` |

That is what lets a future `rv_pccv_vk` build its own surface on the same window
instead of inheriting a `present()` it cannot implement.

### Input must not depend on video

`pump()` currently returns immediately unless `video_ready_`. Once the slots are
independent that becomes a defect: `--mode_cv=null --mode_cio=sdl3` would leave
the ports unpolled. The single early return is removed — the window half is
gated on `video_ready_`, the port poll on `gamepad_ready_`, and event retrieval
stays centralised.

This works because `SDL_INIT_GAMEPAD` implies `SDL_INIT_JOYSTICK`, which implies
`SDL_INIT_EVENTS`: the queue comes up without video.

Two consequences of running with no window. Mouse motion arrives from a window,
so `imouse()` reads zeroes. And `power_off_` is set only by `SDL_EVENT_QUIT` and
`SDL_EVENT_WINDOW_CLOSE_REQUESTED`, both window events, so such a run needs
`--frames` to end.

---

## Console-side methods

They belong to the internal C++ interface, are reachable by the console, and are
never added to the PDK.

| Method | On | Meaning |
| ------ | -- | ------- |
| `valid()` | all six bases | the subsystem came up in the chosen implementation. `true` for every `_null`. On `cd` it does **not** mean a medium is inserted |
| `screen_open()` | `rv_pccv` | `cv_sdl3` opens the window, `cv_null` does nothing |
| `medium_insert()` | `rv_pccd` | putting a medium in the drive is the operator's act, not the disc's |
| `dump_last_frame()` | `rv_pccv` | saving the video subsystem's result. Never reached on `cv_null` — the combination is refused before the run starts |

`valid()` on `cd` cannot answer for the medium even in principle: the console is
asked whether it is ready before the medium is inserted, not after.

### Dumping a frame the machine cannot produce

`--dump-frame` asks for a result. A run whose `cv` is `null` cannot produce one,
and answering with silent success — no file, no error — would be a bad contract:
the user asked for an artefact and got a success that produced nothing. The
refusal that exists today for `--headless` is therefore kept, not deleted. Its
reasoning is already written next to it: "a frame dump would only ever be an
empty frame. Refuse the combination here rather than write a useless file."

What changes is **where** the check lives. Today `--headless` is a plain bool in
`rv_pboot_args`, so the check sits in the argument parser. Once the video slot is
chosen through built-in preset -> preset file -> `--mode` -> `--mode_cv`, a run
can end up with `cv = null` without `--mode_cv` ever being typed. The check
therefore moves past the point where the configuration is resolved and before
the console is constructed, and its message names the outcome rather than a
flag: `cv is null, nothing to dump`.

Giving `dump_last_frame()` an error return instead would report the same fact
after a whole run has already executed for nothing.

---

## What this deletes

`rv_pconsole_params::headless`, `rv_pcca_conf::no_audio`, the two lines that
fill them, the dead `rv_pboot_mode_info::video_enabled`, `rv_pccl::scripting()`,
nine `!sounding_` branches, seventeen `L_ == nullptr` branches, and every
mode branch in the frame loop.

## What this does not cover

`cd` and `cm` were not taken through the module treatment: they have no selector
and no `_null` was designed for them. `cd` already carries modularity one floor
down — `rv_pcmedium` is polymorphic and `rv_pczipmedium` is one implementation
of it.
