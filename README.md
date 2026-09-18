# 3dmppc — PSX-like fantasy console

A fantasy console in the spirit of the PlayStation, written in C++23 with no
engine. It rasterizes entirely on the CPU into a 320×240 framebuffer of 15-bit
colour, plays 24 voices of sound, and boots games from a single-file medium
called a **`.mppcdisc`**.

Games are not built into it. You burn a disc, you insert a disc, the console
runs it — and the console never learns the game's name.

That screen, those voices, and the 1 MB of video memory behind them are a
**virtual budget the console imposes on itself** — not what the host machine
has. The host has gigabytes; a disc gets what the fantasy machine is defined to
have, and the pools really do answer "out of memory" at the line. See the
`rv_manifest_budget_*` defaults in
[`pdk/lib/include/pdklib/rv_manifest/rv_manifest.hpp`](pdk/lib/include/pdklib/rv_manifest/rv_manifest.hpp)
for the whole budget.

---

## The one-minute version

```sh
# 1. build the console
cmake -S . -B build -G Ninja && cmake --build build

# 2. build the developer tools (separate product, separate command)
cmake -S pdk/tools -B pdk/tools/build -G Ninja && cmake --build pdk/tools/build

# 3. burn the sample game into a disc
./pdk/tools/build/mppcburner/mppcburner build mppcdiscs/example-cpp -o build/example-cpp.mppcdisc \
    --baker pdk/tools/build/mppcbaker/mppcbaker

# 4. run it
./build/pconsole/3dmppc build/example-cpp.mppcdisc
```

Press **Esc** (or **Option/Start** on a gamepad) to quit.

Ctrl+C in the terminal stops the console the same ordinary way.

The same three commands work unchanged against
[`mppcdiscs/example-lua/`](mppcdiscs/example-lua/) — swap the directory in
steps 3 and 4 and the console runs a Lua chunk through `rv_cl` instead of
compiled C++.

A Lua disc's C++ side is one pdklib macro — `RV_MPPC_LUA_DISC_DEF("example-lua")`
after `#include "pdklib/rv_dscript/rv_dscript.hpp"`. It forwards each `disc_*`
hook into the same-named Lua function, so the disc's own file carries no
forwarding of its own.

Running `./build/pconsole/3dmppc` with no disc gives you the built-in **service test** — a
diagnostics screen that exercises every subsystem and explains itself on screen.
It is how you tell a broken console from a broken disc.

---

## The five trees, and why there are five

This is the part worth understanding before anything else. Each tree depends
only on the ones above it, and every boundary is enforced by the build rather
than by discipline.

| Tree | What it is | Depends on |
| --- | --- | --- |
| [`pdk/`](pdk/) | **the contract** — the headers describing what the console can do | nothing |
| [`pdklib/`](pdk/lib/) | **disc-side conveniences** written against the contract: matrices, camera, `.obj`, text | `pdk/` |
| `src/` | **the console** — the concrete machine that implements the contract | `pdk/` |
| [`pdk/tools/`](pdk/tools/) | **authoring tools** that turn a directory into a disc | `pdk/` |
| [`mppcdiscs/`](mppcdiscs/) | **the games** | `pdk/`, `pdklib/` |

There is deliberately **no arrow between `src/` and `mppcdiscs/`**: the console
never names a game, and a game never sees a console header. A disc target links
`pdk` and `pdklib` and nothing else, so the first `#include` of a console header
fails to compile rather than being caught in review. See
[`pdk/README.md`](pdk/README.md) for why the contract is shaped this way.

The console and the tools build with **two separate commands** on purpose. The
console is firmware — it loads a disc and runs it. It must never look like the
thing that *compiles* one, and a player's machine needs neither the tools nor
the compiler they drive.

---

## Three things that are easy to confuse

| | What it is | When | Whose machine |
| --- | --- | --- | --- |
| `mppcburner` | the tool that compiles a game and burns a disc | while developing | the developer's |
| `.mppcdisc` | the artifact: compiled `disc.so` + assets, no source inside | sits as a file | — |
| `3dmppc` | the console: `dlopen` and run, **compiles nothing** | while playing | the player's |

---

## Running the console

```sh
./build/pconsole/3dmppc [flags] [DISC.mppcdisc]
```

| Flag | What it does |
| --- | --- |
| `--scale N` | window magnification over the native 320×240 (default 3) |
| `--mode=NAME` | preset: `default` (SDL3 window, pads, sound) or `headless` (no window, no pads, no audio device - the same virtual machine) |
| `--mode_platform=` (`null`\|`sdl3`), `--mode_ca=`/`--mode_cv=` (`null`\|`sw`), `--mode_cio=` (`null`\|`std`), `--mode_cl=` (`null`\|`luajit`) | override one axis of the preset |
| `--mode_cv=null` | no GPU at all, and so no window; pair with `--frames` for a smoke test |
| `--frames N` | stop after N frames (0 = run until quit) |
| `--fixed-step` | fast run: no real-time wait and no audio output; every mode steps 1/60 s per frame, so runs stay reproducible |
| `--disc PATH` | assets **directory** for the built-in disc, which carries no medium of its own; a disc given positionally brings its own medium, so the two may not be combined |
| `--memcard PATH` | memory-card image (default `memcard.mppccard` next to the binary, in `build/pconsole/`) |
| `--mute` | silence the output stage; voices still play as far as the disc can tell |
| `--dump-frame PATH` | write the last rendered frame as a binary PPM (no window needed) |
| `--dev` | attach the development command channel to stdin/stdout; what the console *can* do is set by its build, this only says where to speak |
| `--paused` | start with the frame loop stopped, before frame 0. Lift it with the **Pause** key, or with a resume/step request when `--dev` is given; a mode that offers neither is refused |

Timing: every frame advances the machine by exactly 1/60 s and the SPU renders the audio of that same step, in every mode. Only when the next frame runs differs: with a usable audio device the output queue paces the loop; without one, or once it stalls for 250 ms, the steady clock does; `--fixed-step` does not wait at all and does not feed the audio device.

`--dump-frame` is how you check what the machine actually drew without taking a
screenshot: `magick frame.ppm frame.png` and look at it, or diff it against a
known-good frame.

Logs go to **stderr**, so `2>/dev/null` silences them and `2>log.txt` captures
them. A script's own `print` is routed into the same logger, on stderr, not
stdout — stdout is reserved for the development channel's protocol lines
(see below) and carries nothing when `--dev` is off.

---

## The development runtime

The console can be driven while it runs: stopped at a frame boundary, stepped
one frame at a time, and — the point of the whole thing — handed replacement
Lua for the disc's entry script without losing the game's state.

**The build decides what this console can do; `--dev` only decides where the
channel is attached.** `-D3DMPPC_DEVTOOLS=ON` puts the dev sources in the
compile line and `OFF` puts their null counterparts there instead, the same
slot shape the platform, cv and cl implementations already use — that, and
nothing at run time, is what makes a console capable of being driven. `--dev`
is an I/O choice within a console that already is: it hands **stdin and
stdout** to the protocol. It has to be asked for because those two streams
have another use — a dev build with no `--dev` is an ordinary console you can
pipe like any other — and a player binary refuses the option by name rather
than accepting it and doing nothing. Do not read `--dev` as a feature gate:
there is no capability behind it that the build did not already grant.

What "not in the player build" means, exactly: no implementation, no protocol
vocabulary and no reachable path. Not compiled there: the command dispatcher,
the stdin channel, the protocol's hex encoder, the entry reload, the texture
refresh and the loose-directory mount. `strings` finds none of the verbs and
none of the answer shapes. What remains is the *name* of each slot's entry
point, answering "no" in a handful of bytes — that is the price of choosing a
link-time slot over `#ifdef` in the headers, and it is the same price
`rv_pccl_null`, `rv_pccd_null` and `rv_pcplatform_null` already pay. A
symbol-name sweep is therefore the wrong check; the vocabulary, the refusals
and the size of the remaining stub are the right ones.

The option defines exactly one macro, `RV_DEVTOOLS`, and it guards **data** —
the two fields only the dev slot touches. A slot can leave a build without a
line that reads a member; it cannot remove the member. Every *behaviour* stays
a slot, which is why the frame loop contains no `#ifdef` and a developer tests
the same loop a player runs.

### Stopping it

The **Pause** key stops and starts the frame loop in any mode, with no `--dev`
needed. It is an operator's act on the machine, the same class of input as
closing the window: the disc never sees the key, and what stops is the frame
loop, not something inside the game. A stopped console says so on screen — the
last frame it drew, dimmed, with `CONSOLE PAUSED` across the middle — and
creates no frames at all, so a run started with `--frames N` never reaches its
budget while it is stopped.

### The channel

With `--dev`, stdin becomes a line protocol and stdout answers it, one line
per request, in request order:

```
<id> <verb> [args...]        the id is yours, and must be above zero
<id> ok key=value ...
<id> err error=<token> rv_err=<n> effects=<0|1> msg=<hex>
```

`rv_err` is the contract's own code (`pdk/include/pdk/rv_err.h`) — the same
number a disc would have got from the call — and `error` is the token to branch
on. `msg` is prose for a human, cut to 4096 bytes with the cut named in the
text: a lua error or an `attach` refusal reason is written by the disc, and an
answer that does not fit the queue is an answer nobody reads.

The console echoes your id back on the answer. Zero is not yours to send: the
console tags with `0` the events it raises on its own, so a request numbered
zero would be answered indistinguishably from one of those.

A request carries bytes by ending its header with `bytes <n>`: exactly `n`
bytes follow the newline with no terminator, and the next header starts right
after them. Any value that could hold a space, a newline or a NUL travels as
lowercase hex, which is why the protocol needs no escaping rules at all.

| Request | What it does |
| --- | --- |
| `status` | frame, mode, both hashes, script heap, error count |
| `pause` / `resume` | stop at the next frame boundary / carry on |
| `step` | run exactly one frame, then stay stopped; answered *after* that frame |
| `reload entry` | re-read the entry script off the drive (directory medium only) |
| `reload entry bytes <n>` | the next `n` bytes are the candidate script |
| `asset <name>` | re-read the named texture and refresh it in place behind its residency id; answers with the new `width=`/`height=` (directory medium only) |
| `get <key>` | read one top-level field of the persistent state table |
| `gc` | full collection, then report the heap |
| `quit` | shut down by the ordinary path |

`entry` is a literal selector, not a name: this version replaces the entry
chunk and nothing else. `asset <name>` refreshes a baked texture in place; a
script that changed a non-texture asset gets no notification at all. The
answer carries the texture's size because a RESIZED texture is the one case
the client's own layout has to follow — the game does not have to hear about
it at all, since it asks the drive for the address and the size every draw.

### A session

```sh
mppcburner build mppcdiscs/example-lua --unpacked build/example-lua.discdir --baker …
3dmppc --dev --paused build/example-lua.discdir
```

```
1 status                    -> 1 ok protocol=1 frame=0 mode=paused entry_revision=0 …
2 step                      -> 2 ok completed=1 frame=1 mode=paused
3 get frame_count           -> 3 ok found=1 type=number value=1
                               … edit scripts/example-lua.lua in your editor …
4 reload entry              -> 4 ok entry_revision=1 entry_hash=dac227b078e5407b
5 step                      -> 5 ok completed=1 frame=2 mode=paused
6 get frame_count           -> 6 ok found=1 type=number value=2   <- state survived
7 resume                    -> 7 ok mode=running frame=2
```

An unpacked directory is what makes the file form work: `--unpacked` publishes
the scripts as symlinks to your own sources, so an edit is visible through the
drive at once. An archive cannot change under a running console, so the same
request is refused there rather than answered with a reload of identical bytes
— send the bytes instead.

### What it promises, and what it does not

A candidate becomes the running code only after all of it passes: it compiles,
its body runs, it returns a table, that table has `attach`, and `attach(state)`
returns `true`. A failure at any of those leaves the running code exactly where
it was. **Code is atomic; effects are not** — once a candidate's body or its
`attach` has run it may have written into the state table or called hardware,
and nothing can take that back, which is what `effects=1` on an error answer
says. `attach` returning `false, "reason"` is how a script refuses a state
layout it cannot read: the console has no schema for that table and cannot
detect the mismatch itself.

Not offered: rolling back effects, hot-swapping C++, interrupting a hung game
hook, interrupting a hung C or FFI call, or recovering a session after a
restart.

A chunk may declare `state_shape`, a table literal describing the persistent
state it expects:

```lua
state_shape = {
    frame_count = 0,
    tex_name = "",
    enemies = { ["*"] = { hp = 0, x = 0.0 } },
}
```

Before `attach` runs, the console walks the live state against this shape: a
declared key already stored keeps its value if the type matches; a declared
key missing from state is inserted; a stored key that is not declared is
refused, so nothing is orphaned in silence; a type mismatch is refused, naming
the field path. A table whose only declared key is `"*"` is an open
collection — every key of the matching state table is checked against the
shape under `"*"`, which is how a script declares `enemies` without naming
every id. Allowed values are number, string, boolean and nested table; a
function, coroutine, userdata and a cycle are refused. The two sides are
checked differently, and deliberately: a shape table may be reached again on
a sibling path — that is what `"*"` IS, one template handed to every element
in turn — and refuses only when it is its own ancestor, which is a cycle. A
STATE table reached twice refuses, because two fields holding one table means
a default inserted through one path silently changes the other. No `state_shape` at all skips the check, so a disc built
before this keeps working unchanged.

The walk is two passes: the first validates the whole tree without mutating
anything, the second inserts. If `attach` then refuses, the console removes
exactly the keys it inserted — a refusal this way leaves the state as the old
code left it. What `attach` itself wrote is not undone, per the `effects=1`
contract above. `attach` still returns `false, "reason"` on its own terms:
the console checks structure, only the new code knows whether seconds became
milliseconds. The walk is bounded by a maximum nesting depth and a budget
spent per table visited and per key examined.

A refusal from the shape check answers with the error token `state_shape` and
a message naming the field path, e.g. `screen_width: expected string, stored
number` — with `effects=1`, because the walk runs after the candidate's body
has already executed. The console takes back its own insertions; it cannot
take back what the body did. `state_shape` present but not a table is that
same refusal, not a chunk without a declaration.

### What still needs a restart

| Changed | Restart? | Who notices |
| --- | --- | --- |
| entry Lua chunk | no — `reload entry` | the client, by asking; `entry_revision` counts the successful ones |
| an existing asset's bytes | no — `asset <name>` | the client, by asking; the drive refreshes it |
| an asset added or removed | **yes** | nobody — the drive's name set is fixed at boot |
| `disc.so`, any C++ change | **yes** | the client, comparing `disc_hash` against its own fresh build |
| `disc.toml`, any `[budget.*]` | **yes** | nobody — they are consumed once, at construction |

### Error tokens

Every `err` carries a stable token, so a client branches on that and never on
the sentence. Framing: `protocol`, `payload_size`, `answer_size`,
`payload_timeout`. Machine: `no_machine`, `no_entry`, `not_reloadable`,
`in_call`, `unsupported_medium`,
`nomem`, `insn_ceiling`. A candidate: `compile`, `body`, `not_a_table`,
`no_attach`, `attach`, `attach_refused`, `attach_contract`, `state_shape`. An
asset: `no_asset`, `asset`.

A C API stack imbalance is deliberately not among them: that would be a bug in
the console, not a fault in the script, and reporting it as a script error
sends the developer looking in the wrong place.

The state contract a script has to follow to be reloadable at all — what lives
in the persistent table and what dies with the code — is in
[`mppcdiscs/example-lua/README.md`](mppcdiscs/example-lua/README.md), next to
the script that demonstrates it.

### What an acceptance run has to prove

The console carries no test code, so the harness that checks this runtime lives
outside the repository — it builds both configurations from a working tree,
drives the channel over a fifo so the disc can be EDITED mid-run, and asserts on
protocol lines rather than on log text. What stays here is the list it has to
satisfy, because that list is a property of the contract above and not of
whoever wrote the script:

1. Both configurations build, and both burn and boot an ordinary disc.
2. The player build contains no verb of the protocol and no hex encoder for
   one, its texture refresh is a stub of a few bytes, and it refuses `--dev`
   by name with exit 2 and refuses a loose directory — checked by vocabulary,
   by refusal and by stub size, never by symbol name alone (see the note at
   the top of this section).
3. The medium answers for itself: a positional directory is `live`, an archive
   is `fixed`, and no combination of disc arguments is silently ignored. A
   header that is refused still frames away the payload it claimed, so those
   bytes are never read as commands.
4. `pause` stops at a boundary, `step` runs exactly one frame and is answered
   after it, `resume` carries on — with the frame counter agreeing.
5. A successful reload keeps the state and the next frame runs the new code.
6. Each refusal — a stored key nothing declares, a retyped key, a function or
   a cycle under `"*"`, `attach` refusing on meaning — leaves the old code
   running, the revision unmoved and the keys the shape inserted taken back. A
   many-field shape applies whole or not at all. A candidate with no
   `state_shape` is *accepted*, unchecked, on purpose. A refusal reason too
   long for one answer is cut rather than dropped, and the channel survives it.
   A candidate whose body tries to free the running entry is told to try later,
   and the old code stays callable.
7. Reload repeats within one run without a stack or resource error.
8. A texture refreshes with no game hook and reports its new size; a truncated
   candidate, a non-container and a paletted texture carrying no palette are
   each refused with the old texture still there; a resized one is accepted at
   its new size; an archive refuses the request.
9. A frame is still rendered, and no run crashes — including on the way out,
   after every protocol line has already been printed.

---

## Authoring a game

### The disc directory

```
mygame/
  disc.toml        the manifest: what to compile, what to bake, what to copy
  src/*.cpp        the game — implements rv_de, exports itself with RV_MPPC_DISC_ENTRY_DEF
  assets/          PNGs get baked into texels; everything else is copied in
```

Start by copying [`mppcdiscs/example-cpp/`](mppcdiscs/example-cpp/) — it is the smallest
complete disc and its README walks through what each piece is for.

### disc.toml

```toml
[disc]
id = "mygame"
title = "My Game"

[build]
sources = ["src/*.cpp"]

[assets]
files = ["assets/*.obj"]

[textures]
files = ["assets/*.png"]
format = "idx8"
```

### Burning

```sh
mppcburner build mygame -o mygame.mppcdisc [--baker PATH] [--pdk PATH] [--pdklib PATH]
mppcburner inspect mygame.mppcdisc
```

`inspect` prints the manifest and the entry list to **stdout** (so it pipes into
`grep` cleanly) and diagnostics to stderr.

The burner refuses rather than shipping something broken: a texture larger than
the console allows, assets that overflow the virtual VRAM, or two assets whose
names collide once flattened. Every one of those is cheaper to hit on your desk
than on a player's loading screen.

At runtime a disc can ask the drive for a baked texture by name and get back a
residency id plus its VRAM address, palette address, width and height, instead
of opening and parsing the container itself. The manual `asset_open` /
`asset_size` / `asset_read` path remains for anything that isn't a texture.

### What a disc must contain

Two things make a translation unit a disc rather than a library:

```cpp
class rv_dmain : public rv_pdk::rv_de { /* ... */ };  // implement the lifecycle
RV_MPPC_DISC_ENTRY_DEF(mygame::rv_dmain)      // last line of the file
```

`RV_MPPC_DISC_ENTRY_DEF` plants the two `extern "C"` symbols the console looks up after
`dlopen`; everything else in the disc is hidden. Release what you acquired in
`disc_shutdown()`, not in a destructor — after that hook returns the console may
unload your code, and a destructor belonging to unmapped code cannot run.

---

## Where to read more

| Document | What it covers |
| --- | --- |
| [`pdk/README.md`](pdk/README.md) | the contract: the facade, the five controllers, why the boundary is where it is |
| [`pdklib/README.md`](pdk/lib/README.md) | the disc-side helpers: matrices, camera, transform, `.obj`, text |
| [`pdk/tools/README.md`](pdk/tools/README.md) | the authoring tools: what each one does and why they build separately |
| [`pdk/tools/mppcbaker/README.md`](pdk/tools/mppcbaker/README.md) | the texture format, palette quantization, and the black-vs-transparent trap |
| [`mppcdiscs/example-cpp/README.md`](mppcdiscs/example-cpp/README.md) | the sample disc |
| [`mppcdiscs/example-lua/README.md`](mppcdiscs/example-lua/README.md) | the scripting disc |
| [`mppcdiscs/README.md`](mppcdiscs/README.md) | the disc library |

---

## Where it stands

- **Contract** — closed. Video, audio, drive, memory card, I/O, plus the binary
  ABI a packaged disc is loaded through.
- **Video** — virtual VRAM pool, ordering table, rasterizer for lines / sprites
  / triangles / quads, Gouraud interpolation, affine texture sampling (4/8-bit
  paletted + 15-bit direct, PSX cut-out transparency), 4×4 ordered dithering.
- **Audio** — sound-RAM pool, 24 voices with ADSR, saturating mixer on its own
  thread.
- **Drive** — mounts a directory or a `.mppcdisc` archive behind one interface.
- **Memory card** — 16 slots in a file image, written atomically.
- **Input** — gamepads through SDL, keyboard overlaid on port 0.
- **Packaging** — `mppcburner` compiles a disc directory into a `.mppcdisc`, and
  the console loads it with a two-stage ABI handshake.
- **Scripting** — a disc can declare `[budget.pccl]` and raise Lua chunks
  through `rv_cl`; a script calls the same exported console functions a C++
  disc calls, with no wrapper layer and a LuaJIT heap budget of its own.
- **Not there yet** — semi-transparency and blending, ADPCM / pitch / reverb,
  gyro and trackpads, a contracted RAM budget (video and sound RAM are enforced;
  main RAM is not), the 256×224 display mode.

## Conventions

- **One namespace per tree, and it says who owns the code.** This matters more
  than it looks: a game declared inside the console's namespace reads as part of
  the console, which is exactly the confusion the whole architecture exists to
  prevent.

  | Namespace | Tree | What lives there |
  | --- | --- | --- |
  | `rv_pdk` | `pdk/include` | the contract — both sides depend on it, neither owns it |
  | `rv_pdklib` | `pdk/lib` | the disc-side library |
  | `rv_3dmppc` | `src/` | the console, and nothing else |
  | `rv_pdktools` | `pdk/tools` | the authoring tools |
  | the disc's own id | each packaged disc | `example_cpp::rv_dmain`, `mygame::rv_dmain`, … |
  | `rv_service` | `src/rv_dmain` | the built-in service test — a disc, but a linked-in one |

  A packaged disc needs no prefix and cannot collide with anything: it is built
  with `-fvisibility=hidden` and loaded with `RTLD_LOCAL`, so nothing but its
  two `extern "C"` entry points exists outside it, and two discs may pick the
  same namespace without ever meeting. The built-in service test is the
  exception — it is linked into the console binary and shares its symbols, so it
  takes the project prefix.

  Headers always qualify explicitly (`rv_pdk::rv_cv`); a `using namespace rv_pdk;`
  is allowed only inside a `.cpp`, where it cannot leak into anyone else.

- Left-handed math: `+x` right, `+y` up, `+z` forward.
- Every call across the contract returns `>= 0` on success and a negative
  `rv_err` on failure — callers test with `if (rc < 0)`.
- Design decisions are tagged in the source: `grep -rn "PATTERN:\|THEOREM:" src/ pdk/lib/ pdk/tools/`
  maps every pattern and algorithm to the line that implements it.

## Requirements

SDL3 and LuaJIT (both used from the system if installed, otherwise built from
source on the first configure), CMake 3.24+, Ninja, and a C++23 compiler. The
tools additionally shells out to `cmake` and `ninja` at run time to compile a
disc, and downloads `stb_image.h` into its own build directory.
