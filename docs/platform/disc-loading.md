# Disc loading — build, package, and run a `.mppcdisc`

How a game goes from a directory of source and art to a running disc: the burner
compiles it into its own shared object and packs that plus its assets into one
`.mppcdisc` file; the console mounts the file, loads the code out of it, and
drives it through the disc ABI.

This describes **what the console and the tools do today**. The native path —
manifest, compile, bake, pack, mount, `dlopen`, handshake, teardown — is
implemented and is what `mppcdiscs/example-cpp` exercises end to end. The one part
that is still design rather than code is the **script (Lua) disc kind**, and it
is marked as such at the bottom.

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
| **assets** | do the flattened names stay unique and legal, do the texel budgets hold |
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
editor and VCS bookkeeping, and the two **service names** below are reserved.

The burner also enforces the `[budget]` block — texture dimensions and the
virtual video-memory size the *target machine* will hand out. That budget is the
console's own invention, not a property of the workstation, which is exactly why
it has to be checked deliberately: the host would happily pack far more than the
console will ever accept.

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

The **console** reads only `[disc]`, and only `id`, `title` and `entry`
(`src/rv_pconsole/rv_pcloader.hpp`). Unknown sections and keys are
ignored rather than refused, because the burner deliberately writes more than
the console reads. This asymmetry is a security boundary, not an oversight:
every field the console parses is a field an attacker-supplied archive gets to
influence, so it parses as few as it can, never trusts a length, and treats
`title` as untrusted text that goes through `rv_log_escape` before it reaches a
log line.

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

See [`../../pdk/include/pdk/de/rv_dv.hpp`](../../pdk/include/pdk/de/rv_dv.hpp)
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

## 6. Still design: script discs

The intended shape is **one ABI, two kinds of disc behind it**:

1. **Native disc** — a compiled `disc.so` exporting the entry points from
   `RV_MPPC_DISC_ENTRY_DEF`. This is
   the only kind that exists today.
2. **Script disc** — the archive holds Lua and assets **as data**, and the
   console ships one built-in disc implementation that loads the Lua and calls
   its functions from `frame_update` / `frame_render`. No compilation, no `.so`.

Nothing of this is implemented: there is **no `kind` key** in either manifest
model today, and adding one is the first step.

The reason it is worth the trouble: generating a disc at runtime becomes writing
files into an archive, with no compiler anywhere. The classic split — hot code
(the rasterizer) in C++, game logic in an interpreter — is exactly how PICO-8,
LÖVE and TIC-80 work, and it is adequate for this genre.

The trap to avoid is *scripts → generated C++ → compiled `.so`* **as a loading
mechanism**. That would need a full C++ toolchain on the player's machine and
seconds of compilation per disc. Transpiling Lua to C++ ahead of time is fine —
but as a build-time authoring optimization, never at load.

### Open follow-ups

- A `kind` key, and the built-in script-disc implementation behind it.
- Whether the container version needs to move before either lands.
