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

A Lua disc's C++ side is two lines: `RV_MPPC_DISC_LUA_DEF("example-lua")` after
`#include "pdklib/rv_dscript/rv_dscript.hpp"`, then `RV_MPPC_DISC_ENTRY_DEF` on
the class it defined. The pdklib macro forwards each `disc_*` hook into the
same-named Lua function, so the disc's own file carries no forwarding of its
own. Lua is a convenience, not a layer of the contract: a disc whose hooks are
C++, Rust or anything else writes its own class and plants the same entry
points, and never includes this header.

A C++ disc gets the same kind of convenience from `RV_MPPC_DISC_CPP_DEF`
(`pdklib/rv_cppdisc/rv_cppdisc.hpp`): a base class carrying the startup guards, MENU-button press-edge tracking, `read_asset()`, the
screen's size and the frame plumbing — `frame_begin`/`frame_end`,
`texture_resident` and `draw_sprite` — every C++ disc repeats, that
the disc's own class derives from before being handed to `RV_MPPC_DISC_ENTRY_DEF`.
It has less to carry than the Lua one - a C++
disc's own class already is its `rv_de`, so this macro lives in pdklib alone
and a disc is just as free to skip it and write all five hooks by hand, the
way `mppcdiscs/example-cpp/` did before this macro existed. A
script pulls in another with `require("name")`, which
reads `name.luac` (`name.lua` in an `--unpacked` directory) off the disc, runs
it once and hands every caller the same table.

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
| `--mode_platform=` (`null`\|`sdl3`), `--mode_ca=`/`--mode_cv=` (`null`\|`sw`), `--mode_cio=` (`null`\|`std`), `--mode_cl=` (`null`\|`luajit`), `--mode_cd=` (`null`\|`fs`), `--mode_cm=` (`null`\|`posix`) | override one axis of the preset |
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
one frame at a time, inspected, and - the point of the whole thing - handed
replacement Lua for the disc's entry script without losing the game's state.

**The build decides what this console can do; `--dev` only decides where the
channel is attached.** `-D3DMPPC_DEVTOOLS=ON` puts the dev sources in the
compile line and `OFF` puts their null counterparts there instead, the same
slot shape the platform, cv and cl implementations already use — that, and
nothing at run time, is what makes a console capable of being driven. `--dev`
is an I/O choice within a console that already is: it hands **stdin and
stdout** to the protocol. It has to be asked for because those two streams
have another use — a dev build with no `--dev` is an ordinary console you can
pipe like any other. A player build does not carry the option at all: its
field, its `getopt` entry and its parsing exist only under
`-D3DMPPC_DEVTOOLS=ON`, so `--dev` reaches a player binary's ordinary
unknown-option path — the same diagnostic and exit code any other flag it has
never heard of would get, not a diagnostic naming `--dev` in particular. Do
not read `--dev` as a feature gate: there is no capability behind it that the
build did not already grant.

What "not in the player build" means, exactly: no implementation, no protocol
vocabulary and no reachable path. Not compiled there: the `--dev` option
itself, the command dispatcher, the state inspection, the stdin channel, the
protocol's hex encoder and error line, the entry reload, the texture refresh
and `mount_dir()`, the loose-directory mount — not even its declaration is
visible to a player build any more, so there is no name left to find it by;
the ceiling on hook calls is 0 there. `strings` finds none of the verbs and
none of the answer shapes. What remains, for the slots that still have a
null counterpart to link, is the *name* of each entry point, answering "no"
in a handful of bytes — that is the price of choosing a link-time slot over
`#ifdef` in the headers, and it is the same price `rv_pccl_null`,
`rv_pccd_null` and `rv_pcplatform_null` already pay. A symbol-name sweep is
therefore the wrong check; the vocabulary, the refusals and the size of the
remaining stub are the right ones.

The option defines no macro at all: it chooses which files the console is
built from and nothing else. What a slot cannot remove is a **member** - a
build can be left without the line that reads a field, but not without the
field, so `--dev`'s own flag and the handful of members the dev dispatcher
keeps live in every build's headers and cost a player the bytes they occupy.
Every *behaviour* stays a slot, which is why the frame loop contains no
`#ifdef` and a developer tests the same loop a player runs.

A loose directory is a **`--dev` privilege on top of the build**, not a
substitute for it: a dev build handed a directory without `--dev` refuses it
exactly as a player build always does, by name, rather than mounting what
`--dev` exists to gate. The dev half of `rv_pboot_disc_mount()`
(`rv_pboot_discmedium_devtools.cpp`) is where that refusal lives, and it is told
whether `--dev` was given by its caller — never by reaching into a global —
so the same function stays answerable the same way regardless of who asks.

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
text: a lua error written by the disc's own script, or a `state_shape`
refusal naming the field the console's own walk checked, and an answer that
does not fit the queue is an answer nobody reads. An error the
channel raises while framing a request - an unreadable or zero id, a payload
size it cannot accept, a payload that never arrives - answers for the channel,
not for a contract call, and carries no `rv_err`.

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
| `reload module <name>` | re-read the module `require("<name>")` loaded off the drive and update it in place (directory medium only) |
| `reload module <name> bytes <n>` | the next `n` bytes are the new version of that module |
| `asset <name>` | refresh the named texture in place: `resident=1` with the new `width=`/`height=`, or `resident=0` when nothing holds it resident and there is nothing to refresh; a name a SOUND holds resident is refused with `unsupported_kind` (directory medium only) |
| `get <key> [<key> ...]` | read the value at a path into the persistent state table, one key per level; a table answers with its `count=` |
| `keys [<key> ...]` | list the keys of the table at a path - no path lists the state table itself - with their value types |
| `gc` | full collection, then report the heap |
| `quit` | shut down by the ordinary path |

`entry` selects the chunk the manifest names; `module <name>` selects a module
by the name it was `require`d under. The entry answers with `entry_revision=`
and `entry_hash=`, a module with `module=<hex>` and `hash=`. A module goes
through the same compile, body and table checks as the entry, but has no
state of its own. A file that inherits from a reloaded module needs no
reload of its own: it holds the same table, now updated in place.

A path is looked up raw, one table per key: a key is tried as a string, and
when that misses and it spells a decimal integer within 2^53, as that integer - so
`get enemies 3 hp` reaches `state.enemies[3].hp`. A path that runs out of
tables answers `found=0 type=nil`; at most 32 keys. `keys` answers
`found= type= count= shown= keys=`, each entry `s<hex>:<type>` for a string
key, `i<n>:<type>` for an integer key and `x:<type>` for a key no path can
name; `shown` below `count` means the list was cut to fit one answer.

`asset <name>` refreshes a baked texture in place; a
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
its body runs, it returns a table, and the live state table still matches the
shape the console remembers from the last time it checked (below — no shape
remembered yet, at first boot, is a pass by construction: whatever
`disc_initialize` leaves in `state` right then IS the shape). A failure at any
of those leaves the running code exactly where it was, AND leaves `state`
exactly where it was: the candidate's body already had `state` reachable from
its own environment before any of this runs, so the console snapshots it
first and puts it back if the candidate is refused (`rv_pccl_luajit`'s
`snapshot_state_`/`restore_state_`). **Code is atomic; state is restored on a
shape refusal; nothing else is** — a candidate's body may still have called
hardware, and nothing can take THAT back, which is what `effects=1` on an
error answer says regardless of which check finally refused it. There is no
further, script-side veto: the shape check is structural only, and the
console hands a passing candidate's own environment the state table directly
rather than asking the candidate to accept delivery of it — so nothing here
can refuse a structurally valid state on meaning alone.

Once every check has passed, the running tables are updated in place rather
than replaced. The table the file returned, and every table inside it that the
old version also had under the same key - metatables included - stay the same
objects and receive the new functions; a function the new version no longer
defines is removed from them; and the new code's references to its own fresh
tables are pointed at the live ones. So an object in state whose metatable is
a class keeps its class and runs the new methods on the next frame, and an
object made after the reload gets that same class. Three rules follow for the
script author:

- a class follows a reload only if it can be reached from the table the file
  returns; a local table the file does not hand out is new after every reload;
- non-function values in class tables and a file's local tables come from the
  new version, so data that must survive a reload lives in state;
- a function copied elsewhere (`local f = Entity.speak`, a callback) keeps the
  old code - call through the table.

The state table and the modules `require` has loaded are live data, not part of
the candidate: the in-place walk does not enter them.

Not offered: rolling back a hardware call, hot-swapping C++, interrupting a
hung C or FFI call, or recovering a session after a restart. A hung Lua hook
is interrupted in a development build only: every hook call runs under the
same instruction ceiling as a reload candidate, and a call that runs past it
fails like any raised error - with `--dev` the loop stops on a
`script_error` event. The count hook that takes is not free, so a Lua-heavy
frame runs measurably slower in a development build; a player build arms no
ceiling.

No chunk declares a shape any more. The console reads one for itself, once,
from the live state table itself, right after the entry chunk's first
`disc_initialize` call has returned - the moment a script has finished
setting `state` up is also the moment the console has something worth
remembering. What it remembers is a name and a value type for every key
`state` holds, at every depth. A later reload candidate is held to exactly
that:

- a key whose type changed is refused, naming the field path, e.g.
  `frame_count: expected number, found string`;
- a key the new code added is accepted, and joins what the console
  remembers from then on;
- a key the new code stopped using is accepted too, and quietly drops out of
  what the console remembers - the new code is the one that gets to say what
  `state` still needs.

Allowed values are number, string, boolean and table; a function, a
coroutine, userdata and a thread are refused outright, wherever in the tree
they turn up - any of them would keep the old chunk's bytecode alive past a
reload that was supposed to have replaced it. The walk that finds all this is
bounded by a maximum nesting depth and a budget spent per table visited and
per key examined - the same two bounds a runaway or adversarial `state` was
always going to need, now doing double duty as the only cycle guard the walk
has: a table containing itself just recurses until the depth bound refuses
it.

A refusal from the shape check answers with the error token `state_shape` and
a message naming the field path, e.g. `frame_count: expected number, found
string` — with `effects=1`, because the walk runs after the candidate's body
has already executed and may have called hardware; `state` itself, though, is
exactly what it was before that body ran, because the console snapshots it
first and puts it back (above). Nothing is remembered from a refused walk: a
partial reshaping of the console's memory of the shape would be worse than no
reshaping at all.

### What still needs a restart

| Changed | Restart? | Who notices |
| --- | --- | --- |
| entry Lua chunk | no — `reload entry` | the client, by asking; `entry_revision` counts the successful ones |
| a Lua module | no - `reload module <name>` | the client, by asking; every file that required it sees the new code |
| an existing texture's bytes | no — `asset <name>` | the client, by asking; the drive refreshes it |
| an existing sound's bytes | **yes**, once it is resident | the client: `asset` on it answers `err unsupported_kind`. A voice is already reading that block, and moving the bytes under its read head is not something the drive can undo |
| an asset added | no | the code that asks for it: the drive looks a name up when it is opened, so reloaded code can acquire it |
| an asset removed | no | a resident texture keeps its last good copy and `asset` on it answers `err asset`; a new open or acquire gets `RV_ERR_NOENT` |
| `disc.so`, any C++ change | **yes** | the client, comparing `disc_hash` against its own fresh build |
| `disc.toml`, any `[budget.*]` | **yes** | nobody — they are consumed once, at construction |

### Error tokens

Every `err` carries a stable token, so a client branches on that and never on
the sentence. Framing: `protocol`, `payload_size`, `answer_size`,
`payload_timeout`. Machine: `no_machine`, `no_entry`, `no_module`, `not_reloadable`,
`in_call`, `unsupported_target`,
`unsupported_medium`, `drive`, `nomem`, `insn_ceiling`. A candidate:
`bad_request` (no bytes), `compile`, `body`, `not_a_table`, `state_shape`. An
asset: `no_asset`, `asset`, `unsupported_kind`.

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
   one, its texture refresh is a stub of a few bytes, `--dev` is not an
   option it recognises at all (exit 2 as an unrecognised flag, not a
   diagnostic naming it), and it refuses a loose directory by name; a dev
   build refuses that same directory too, the same way, unless `--dev` was
   also given — checked by vocabulary, by refusal and by stub size, never by
   symbol name alone (see the note at the top of this section).
3. The medium answers for itself: a positional directory is `live`, an archive
   is `fixed`, and no combination of disc arguments is silently ignored. A
   header that is refused still frames away the payload it claimed, so those
   bytes are never read as commands.
4. `pause` stops at a boundary, `step` runs exactly one frame and is answered
   after it, `resume` carries on — with the frame counter agreeing.
5. A successful reload keeps the state and the next frame runs the new code.
6. A retyped key refuses a reload — old code running, revision unmoved, and
   `state` put back exactly as it read before the candidate's body ran. A new
   key is accepted and remembered; a key the new code stopped using is
   accepted and forgotten. A candidate is accepted, unchecked, the first time
   the console has nothing remembered to compare against yet (first boot). A
   refusal reason too long for one answer is cut rather than dropped, and the
   channel survives it. A candidate whose body tries to free the running
   entry is told to try later, and the old code stays callable.
7. Reload repeats within one run without a stack or resource error.
8. A texture refreshes with no game hook and reports its new size; a truncated
   candidate, a non-container and a paletted texture carrying no palette are
   each refused with the old texture still there; a resized one is accepted at
   its new size; an archive refuses the request.
9. `get` walks a path into nested tables, `keys` lists a table's keys with
   their types, and a path through a non-table answers `found=0`.
10. In a development build a Lua hook that never returns ends as a
    `script_error` event with the state intact, and fixed code reloads after
    it. An asset added after boot is acquirable without a restart, and `asset`
    answers `resident=0` for it until something holds it.
11. `reload module` updates a required module in place: an object whose class
    inherits from it in another file runs the new code on the next frame, and
    a module nobody required is `no_module`.
12. A frame is still rendered, and no run crashes — including on the way out,
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

[scripts]
sources = ["scripts/*.lua"]

[assets]
files = ["assets/*.pcm"]

[textures]
files = ["assets/*.png"]
format = "idx8"
```

`[scripts]` belongs to a Lua disc and `[build]` to a C++ one; a disc may carry
both. Every `[budget.*]` section is optional - a disc that states none is held to the
console's own built-in budget, a default-constructed `rv_manifest_budget`. Both
example discs state theirs explicitly, and
[`mppcdiscs/example-lua/disc.toml`](mppcdiscs/example-lua/disc.toml) says why
each number is what it is.

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

At runtime a disc can ask the drive for a resource by kind and name and get
back where it now lives, instead of opening and parsing the container itself.
A texture answers a VRAM address, a palette address and its dimensions; a
sound answers a sound-memory address and its length in bytes, ready for
`rv_voice_conf.sample_address`. A disc never acquires or releases either, only
names one: the drive makes it resident on the first ask and frees everything
it made resident, on its own, when the disc unloads. A sound carries no
container at all - raw PCM is copied onto the disc as it is, so there is
nothing to bake and nothing to parse. The manual `asset_open` / `asset_size` /
`asset_read` path remains for whatever the drive does not make resident.

### What a disc must contain

Two things make a translation unit a disc rather than a library:

```cpp
class rv_dmain : public rv_pdk::rv_de { /* ... */ };  // implement the lifecycle
RV_MPPC_DISC_ENTRY_DEF(mygame::rv_dmain)      // last line of the file
```

`RV_MPPC_DISC_ENTRY_DEF` plants the two `extern "C"` symbols the console looks up after
`dlopen`; everything else in the disc is hidden. It lives in pdk, not pdklib:
every disc must call it, whether or not it uses any pdklib convenience.
Release what you acquired in
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
- **Audio** — sound-RAM pool, 24 voices with ADSR, saturating mixer, rendered
  on the console thread once per frame.
- **Drive** — mounts a directory or a `.mppcdisc` archive behind one interface.
- **Memory card** — 16 slots in a file image, written atomically.
- **Input** — gamepads through SDL, keyboard overlaid on port 0.
- **Packaging** — `mppcburner` compiles a disc directory into a `.mppcdisc`, and
  the console loads it with a two-stage ABI handshake.
- **Scripting** — a disc can declare `[budget.pccl]` and raise Lua chunks
  through `rv_cl`; a script calls the same exported console functions a C++
  disc calls, with no wrapper layer and a LuaJIT heap budget of its own.
- **Development runtime** — an optional build (`-D3DMPPC_DEVTOOLS=ON`) that
  boots a loose disc directory and answers a line protocol on stdin: pause,
  step and resume at a frame boundary, reload of the Lua entry chunk and of
  a required module, state inspection and a texture refresh.
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
