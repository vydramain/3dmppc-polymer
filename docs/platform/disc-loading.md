# Disc loading — build, package, and run a `.mppcdisc`

How a game goes from a directory of source and art to a running disc: the burner
compiles it into its own shared object and packs that plus its assets into one
`.mppcdisc` file; the console mounts the file, loads the code out of it, and
drives it through the disc ABI.

This describes **what the console and the tools do today**. The native path —
manifest, compile, bake, pack, mount, `dlopen`, handshake, teardown — is
implemented and is what `mppcdiscs/example-cpp` exercises end to end. Scripting
rides that same path rather than a second one: a disc that carries Lua still
burns and loads exactly this way, still ships one `disc.so`, and it is that
`disc.so` which drives a script machine the console hands out — never a
different kind of disc the console loads differently. `mppcdiscs/example-lua`
exercises that path end to end, and §6 covers it.

> Scope: this is both documents the platform README once listed as planned —
> the boundary (`disc-abi.md`) and the package (`disc-format.md`).

---

## The whole path

```sh
cmake -S . -B build -G Ninja && cmake --build build            # the console
cmake -S pdk/tools -B pdk/tools/build -G Ninja \
  && cmake --build pdk/tools/build                             # the tools

./pdk/tools/build/mppcburner/mppcburner build mppcdiscs/example-cpp \
    -o build/example-cpp.mppcdisc --baker pdk/tools/build/mppcbaker/mppcbaker

./build/3dmppc build/example-cpp.mppcdisc
```

The console never names a concrete game. It takes a disc path as its first
positional argument, mounts the archive, loads the code, and runs it. With no
argument it falls back to the built-in `rv_dmain` — the service test, which is a
disc but a linked-in one, not a game.

`--disc PATH` is the separate development shortcut: it mounts a **directory** of
loose assets as the medium, so art can be iterated without a packaging step. A
packaged `.mppcdisc` goes in the positional argument instead and brings its own
medium with it.

---

## 1. Burning — four gates, not four stages

`mppcburner build` reads `disc.toml` from the disc directory and does four
things, each of which is a **gate**: a disc that survives all four cannot fail
the console for a reason the burner could have seen.

| Gate | What it settles |
| --- | --- |
| **manifest** | is this disc's `id` a safe filename, and is the texture format one that exists |
| **compile** | does the game build against `pdk/` and `pdklib/` and **nothing from `src/`** |
| **assets** | do the flattened names stay unique and legal, do the texel budgets hold, does `[budget.pccl]` agree with what `[scripts]` actually planned, does every `.lua` compile |
| **burn** | write the container |

The compile gate generates a small CMake project — by default in
`<disc-dir>/.mppcburn/`, `--keep-build=PATH` puts it elsewhere and leaves it
behind — and drives `cmake` and `ninja` to build the manifest's `sources` into
one module. The target is set with `PREFIX ""` and `OUTPUT_NAME "disc"` so the
result is exactly `disc.so`; the default `libdisc.so` would be unloadable, and
for a reason that reads as a mystery at the far end.

The asset gate is where the flat namespace is enforced. `rv_cd` resolves a
resource **by name, with no separators**, so `assets/enemies/smoke.png` becomes
`smoke.mppctex` inside the archive. That flattening can collide, and a collision
is refused with both originals named — "two `.mppctex` entries collide" would say
nothing about which two PNGs to rename. Names starting with a dot are refused as
editor and VCS bookkeeping, and the two **service names** below are reserved. A
disc's `.lua` sources flatten the same way, into `.luac` bytecode, in the same
namespace — a script and a texture can collide on one archive name exactly as
two textures can.

The burner also enforces the `[budget]` block — texture dimensions and the
virtual video-memory size the *target machine* will hand out. That budget is the
console's own invention, not a property of the workstation, which is exactly why
it has to be checked deliberately: the host would happily pack far more than the
console will ever accept.

A disc that carries scripts is checked the same way, plus one thing textures
are never asked: **presence must agree**. `[scripts] sources`,
`[budget.pccl] script_memory_size` and `[budget.pccl] script_entry` are one
declaration in three fields — a manifest stating some of them and not the rest
describes a machine nobody can build, and the burner refuses it by name, listing
which of the three is stated and which is missing. `script_entry` must also name
one of the scripts the glob actually found. Only once all of that holds does the
burner compile each `.lua` to `.luac` bytecode — with the LuaJIT it is *linked
against*, never a `luajit` found on `$PATH`, because bytecode is version-specific
and tying it to anything but the linked library would tie a disc's bytecode to
whichever machine happened to burn it.

`mppcburner inspect DISC.mppcdisc` prints the manifest and the entry list to
**stdout** (so it pipes into `grep`) and diagnostics to stderr, without
unpacking anything.

---

## 2. The container

A `.mppcdisc` is a plain zip written with the **STORE method only — never
compressed**.

That is a deliberate limit, not a missing feature. The console has to read this
archive, and a stored entry is read with one seek and one read straight from the
central directory's offset. A deflated one would put zlib inside the console — a
dependency the machine otherwise does not have, in the component that must stay
smallest and most auditable. The medium is also the model: a CD-ROM held its
data as it was, and the texture pipeline already assumes bytes it can use
directly. If compression ever earns its place it arrives as a bump of the
container version, not as a change to this one.

Two entry names are **service names**, read by the loader before it looks at any
asset, and therefore forbidden to assets:

| Entry | What it is |
| --- | --- |
| `disc.toml` | the manifest, re-rendered by the burner into the archive |
| `disc.so` | the compiled game; the default value of the manifest's `entry` |

Writer: `pdk/tools/mppcburner/rv_zipwrite.*`. Reader: `src/rv_pconsole/cd/rv_pczip.*`.

---

## 3. The manifest, and why the two sides read different amounts of it

The disc directory holds a `disc.toml` in a **subset of TOML** — sections,
`key = "string"`, `key = 42`, arrays of strings. The parser refuses what it does
not handle rather than guessing, and its errors name the line number: a manifest
is written by hand, and *"line 14: unknown key 'source' (did you mean
'sources'?)"* is the difference between a fixed typo and an afternoon.

```toml
[disc]
id = "example-cpp"
title = "mppcdisc example"

[build]
sources = ["src/*.cpp"]

[assets]
files = ["assets/*.txt"]

[textures]
files = ["assets/*.png"]
format = "idx8"
```

The **burner** reads all of it (`pdk/tools/mppcburner/rv_burner_manifest.hpp`): build
globs, defines, include dirs, assets, textures, budget.

The **console** parses the *same* schema — burner and console share one
`pdklib/rv_manifest` model, so an unknown section or key is refused on either
side alike, never silently ignored (`src/rv_pconsole/rv_pcloader.cpp`) — but it
only *acts on* part of what that parse hands back: `id`, `title`, the code
entry's name, and the whole `[budget]` block, `[budget.pccl]` included (§6).
`[build] sources` parses into the struct exactly like everything else, and the
console never once reads it back out. That is still a narrower trust boundary
than a symmetrical read would be: every field the console *acts on* is a field
an attacker-supplied archive gets to influence, so `title` goes through
`rv_log_escape` before it reaches a log line, and no length coming out of the
manifest is ever trusted unchecked.

---

## 4. Loading — the extraction that cannot be avoided

**A `.so` cannot be `dlopen`ed from inside the archive.** `dlopen` does not take
bytes, it takes a *path*: the dynamic loader maps the code with `mmap()`, which
needs a real inode the kernel can back the mapping with, page by page, for as
long as the code is resident. An entry inside a zip is a byte range belonging to
another file — there is nothing to map, and the loader offers no interface
through which the console could hand it a buffer.

So the code entry is extracted to a private temporary file first, and that file
is what `dlopen` sees:

```
console opens example-cpp.mppcdisc (a stored zip)
 -> reads disc.toml for the archive entry
 -> reads disc.so, checks its ELF note against RV_MPPC_VER_MAJOR/MINOR
 -> extracts the `entry` (disc.so) to a private temporary file
 -> dlopen(that path, RTLD_NOW | RTLD_LOCAL)
 -> dlsym rv_mppc_disc_entry_create_fn / rv_mppc_disc_entry_destroy_fn
 -> create() -> rv_de*
```

It costs one write of a few hundred kilobytes at boot, once, and nothing per
frame. The Linux-only `memfd_create` + `/proc/self/fd/N` trick removes the disk
round trip and is **deliberately not used**: it is not portable, and the
portable path is the one that must work.

`RTLD_LOCAL` keeps the disc's symbols out of the global namespace. A disc is
compiled with `-fvisibility=hidden`, so the only two things reachable by `dlsym`
are the ones `RV_MPPC_DISC_ENTRY_DEF` planted.

### The handshake happens before `dlopen`

The console reads the version note embedded in `disc.so` and compares its major
and minor versions with `RV_MPPC_VER_MAJOR/MINOR` before loading executable
code. A mismatched disc is rejected before its constructors can run. This keeps
an incompatible contract from becoming garbage geometry, silent corruption, or
a crash three minutes later with no clue as to why.

### Teardown has exactly one correct order

`rv_pcloader` owns the temporary file, the `dlopen` handle and the disc object as
**one indivisible ownership** (PATTERN: RAII), so no caller taking an early
return can get this wrong:

```
1. disc_shutdown()          the disc's last chance to touch the facade
2. destroy(disc)            the disc's own destructor, from the disc's code
3. dlclose(handle)          the code is unmapped only now
4. unlink(temporary file)   the inode goes last, and only then
```

Steps 2 and 3 in that order are not a preference. `dlclose()` may unmap the
library's text segment, and the destructor *lives in* that text segment —
destroying after `dlclose` is a jump into an unmapped page: a segfault on
shutdown, where nobody is looking. Step 1 precedes step 2 for the same reason:
`disc_shutdown` is a virtual on an object step 2 is about to end. And the
destructor must be the **disc's own**, never the console's `delete`: the disc
allocated the object with its allocator, out of its heap, and only its code
knows the complete type.

`disc_shutdown` is owed only by a disc whose `disc_initialize` returned success,
and the loader compares pointers rather than trusting the call — the console may
be running a disc this loader did not produce (the built-in `rv_dmain`), and that
one's lifecycle is none of the loader's business.

---

## 5. Why the gate is `extern "C"`

The **C++ ABI is not stable** across compilers, compiler versions, standard
libraries, or even optimization flags: name mangling, vtable layout, exception
propagation and RTTI representation all differ. A C++ symbol is therefore a poor
door between two independently built binaries. `extern "C"` has none of that
freedom — one symbol name, one calling convention.

Once the console holds the returned `rv_de*`, both sides are inside **one
process** again and the vtable is internally consistent, which is why a C++
*object* may cross a boundary that a C++ *function* may not.

A disc writes one line at the bottom of its translation unit:

```cpp
class rv_dmain : public rv_pdk::rv_de { /* ... */ };
RV_MPPC_DISC_ENTRY_DEF(example_cpp::rv_dmain)
```

See [`../../pdk/include/pdk/de/rv_dv.h`](../../pdk/include/pdk/de/rv_dv.h)
for the version constants, entry-point names, and the macro itself.

### The thickness decision, settled

There was a real choice here: a **thin** ABI where only the contract crosses and
the disc carries its own drawing code, or a **shared runtime** where console and
disc both link a common `libmppc_runtime.so`.

The thin one won, and the build enforces it. A disc target links `pdk` and
`pdklib` and nothing else, so the first `#include` of a console header fails to
compile rather than being caught in review. The cost is that disc-side helpers
are duplicated into every disc — they are tiny, and it buys a boundary that
cannot rot: no "this disc needs console runtime 1.4" ever exists.

---

## 6. Scripting: one more controller, not a second kind of disc

There is **no `kind` key**, in either manifest model, and none is needed: every
disc — Lua or not — is the exact native disc §5 describes, one compiled
`disc.so` exporting the entry points `RV_MPPC_DISC_ENTRY_DEF` planted. There is
no built-in disc implementation that loads Lua on a disc's behalf.
`mppcdiscs/example-lua/src/example-lua.cpp` **is** that disc's `disc.so`; its
own hooks do nothing but forward into a chunk of Lua the console never parses a
line of and never learns is involved — the console only ever talks to a
`disc.so`, and what that `disc.so` does with what it was handed is the disc's
business.

What the `disc.so` is handed is a controller: `rv_cl`
([`../../pdk/include/pdk/cl/rv_cl.h`](../../pdk/include/pdk/cl/rv_cl.h)), the
Lua machine, opaque C like every other controller in the contract. The console
hands it out or withholds it, the same shape `rv_pcca` withholds itself under
`--no-audio` — a disc that states no `[budget.pccl]` gets `rv_pdko_cl() ==
nullptr` and runs exactly as it did before scripting existed.

### The all-or-none invariant, checked twice

A manifest declares a lua disc with **three statements that mean nothing
apart**: where the scripts come from, how much memory they run in, and which
one starts. The burner checks this at pack time (§1 above,
`pdk/lib/include/pdklib/rv_manifest/rv_manifest.cpp`), and the console checks
it again at mount time, from the archive's own bytes
(`rv_pcloader::mount`, `src/rv_pconsole/rv_pcloader.cpp`) — the manifest the
burner validated and the manifest sitting inside this particular archive are
not provably the same manifest, and parsing an archive's `disc.toml` never
runs the burner's validation, so this is the only place the console enforces
it at all:

> `[scripts] sources` non-empty, `[budget.pccl] script_memory_size > 0`, and
> `[budget.pccl] script_entry` non-empty must all hold, or none of them may. A
> manifest carrying some of the three describes a machine the console cannot
> build, and the honest answer is a refusal, not a guess at which of the three
> the author meant.

When `script_entry` is stated, the loader additionally confirms the archive
actually holds an asset by that name — a disc naming an entry the drive cannot
produce is refused before a single line of Lua runs.

Only `disc.so` is checksummed against the version note the burner stamped
(§4's handshake). A `.luac` swapped out of the archive after burning, or a
`disc.toml` hand-edited to claim a different budget, moves no checksum at all
— the all-or-none check above, run fresh from the bytes every time, is what
stands between the console and a lua declaration it cannot honour, not a hash.
Whatever the entry chunk itself pulls in afterwards is not checked and is not
meant to be: the console knows the one name its manifest gave it, and nothing
past that.

### What the machine looks like once it is up

A disc whose `[budget.pccl] script_memory_size` is `0` — the field nobody
wrote — gets no VM at all (`rv_pccl::valid()`,
`src/rv_pconsole/cl/rv_pccl.cpp`). A disc that does declare a budget gets a
`lua_State` opened with a **budgeted allocator**: growth that would push the
machine past `script_memory_size` fails the allocation rather than reaching
the host's heap, so the number in the manifest is a ceiling the VM itself is
held to, the same way `rv_cv`'s pool is held to `video_memory_size`. Exactly
four stock libraries are opened — `base`, `string`, `math`, `table` — plus
`ffi`, and `ffi` is not this console's sandbox: `pdk.cast`/`pdk.new`
(`ffi.cast`/`ffi.new`, wired up in `RV_PCCL_PDK_BOOTSTRAP_SRC`,
`src/rv_pconsole/cl/rv_pccl.cpp`) hand a script the same raw read/write over
this process's memory its own `disc.so` already has. Withholding `io`, `os`
and `require` — and never exposing `ffi.cdef` itself past bring-up, so a
script can declare no new host function to call — closes the *accidental*
doors instead: a script that calls `os.exit` or opens a file by name fails
immediately. There is no boundary here for that to protect in the first
place: a disc's scripts are written by the same author as the `disc.so`
carrying them, and that `disc.so` already runs as native code in this same
process, so a script is never less trusted than the code that raised it.

The console's own contract is what that state gets instead. At bring-up the
console feeds the VM two strings generated from the PDK headers themselves
(`cmake/rv_cdef_gen.cmake`, sliced from the `RV_CDEF_BEGIN`/`RV_CDEF_END`
regions every contract header marks): `rv_pdk_cdef`, fed straight to LuaJIT's
`ffi.cdef`, and `rv_pdk_consts`, Lua source carrying the PDK's `#define RV_*`
constants that `ffi.cdef` cannot express. Both become one global table, `pdk`
— `pdk.cv_frame_put(cv, prim)`, `pdk.TEXWRAP_CLAMP` — resolved lazily against
`ffi.C` and memoised on first use; a name matching no hardware symbol raises
loudly instead of reading back `nil`. `pdk` is the only thing besides the four
stock libraries this console puts in `_G`.

The disc still drives its own hooks — scripting does not move
`disc_initialize` / `frame_update` / `frame_render` / `disc_shutdown` into the
console. `rv_cl_script_entry()` raises the one chunk the console already
resolved and verified from the manifest, so the disc never spells the name
again; the disc's own hooks then call into it with `rv_cl_script_call`,
exactly the shape `mppcdiscs/example-lua` uses to forward `dt` in each frame
and read a `release` boolean back out.

### The trap this design still avoids

*Scripts → generated C++ → compiled `.so`*, **as a loading mechanism**, is
still not what happens, and still should not: it would need a full C++
toolchain on the player's machine and seconds of compilation per disc.
`mppcburner` compiles Lua to bytecode ahead of time, at burn time, on the
author's own machine (§1) — the console only ever `loadbuffer`s bytes it was
handed, the same shape a compiled `.so` and a compiled `.luac` already both
are. Transpiling Lua to C++ ahead of time would still be fine as a build-time
authoring optimization; it is just never a load-time one.
