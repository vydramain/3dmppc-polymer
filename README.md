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
have, and the pools really do answer "out of memory" at the line. See
[`docs/platform/specs.md`](docs/platform/specs.md) for the whole budget and for
the two places it is not yet enforced.

---

## The one-minute version

```sh
# 1. build the console
cmake -S . -B build -G Ninja && cmake --build build

# 2. build the developer tools (separate product, separate command)
cmake -S pdk/tools -B pdk/tools/build -G Ninja && cmake --build pdk/tools/build

# 3. burn the sample game into a disc
./pdk/tools/build/mppcburner/mppcburner build mppcdiscs/hello -o build/hello.mppcdisc \
    --baker pdk/tools/build/mppcbaker/mppcbaker

# 4. run it
./build/3dmppc build/hello.mppcdisc
```

Press **Esc** (or **Option/Start** on a gamepad) to quit.

Running `./build/3dmppc` with no disc gives you the built-in **service test** — a
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
./build/3dmppc [flags] [DISC.mppcdisc]
```

| Flag | What it does |
| --- | --- |
| `--scale N` | window magnification over the native 320×240 (default 3) |
| `--headless` | no window; pair with `--frames` for a smoke test |
| `--frames N` | stop after N frames (0 = run until quit) |
| `--fixed-step` | feed the disc a fixed 1/60 dt — reproducible runs |
| `--disc PATH` | mount a **directory** of loose assets: the development shortcut, no packaging step |
| `--memcard PATH` | memory-card image (default `memcard.mppccard` in the working directory) |
| `--mute` | silence the output stage; voices still play as far as the disc can tell |
| `--dump-frame PATH` | write the last presented frame as a binary PPM |

`--dump-frame` is how you check what the machine actually drew without taking a
screenshot: `magick frame.ppm frame.png` and look at it, or diff it against a
known-good frame.

Logs go to **stderr**, so `2>/dev/null` silences them and `2>log.txt` captures
them while the program's own output stays on stdout.

---

## Authoring a game

### The disc directory

```
mygame/
  disc.toml        the manifest: what to compile, what to bake, what to copy
  src/*.cpp        the game — implements rv_de, exports itself with RV_DISC_EXPORT
  assets/          PNGs get baked into texels; everything else is copied in
```

Start by copying [`mppcdiscs/hello/`](mppcdiscs/hello/) — it is the smallest
complete disc and its README walks through what each piece is for.

### disc.toml

```toml
[disc]
id = "mygame"
title = "My Game"
abi_version = 1

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
the console allows, assets that overflow the virtual VRAM, an ABI version the
console does not speak, or two assets whose names collide once flattened. Every
one of those is cheaper to hit on your desk than on a player's loading screen.

### What a disc must contain

Two things make a translation unit a disc rather than a library:

```cpp
class rv_dmain : public rv_pdk::rv_de { /* ... */ };  // implement the lifecycle
RV_DISC_EXPORT(mygame::rv_dmain)              // last line of the file
```

`RV_DISC_EXPORT` plants the two `extern "C"` symbols the console looks up after
`dlopen`; everything else in the disc is hidden. Release what you acquired in
`disc_shutdown()`, not in a destructor — after that hook returns the console may
unload your code, and a destructor belonging to unmapped code cannot run.

---

## Where to read more

| Document | What it covers |
| --- | --- |
| [`docs/README.md`](docs/README.md) | **console vs disc** — read this first |
| [`pdk/README.md`](pdk/README.md) | the contract: the facade, the five controllers, why the boundary is where it is |
| [`pdklib/README.md`](pdk/lib/README.md) | the disc-side helpers: matrices, camera, transform, `.obj`, text |
| [`docs/platform/specs.md`](docs/platform/specs.md) | the hardware spec, and every place it deliberately differs from a real PSX |
| [`docs/platform/disc-loading.md`](docs/platform/disc-loading.md) | how a disc is packaged and loaded |
| [`pdk/tools/README.md`](pdk/tools/README.md) | the authoring tools: what each one does and why they build separately |
| [`pdk/tools/mppcbaker/README.md`](pdk/tools/mppcbaker/README.md) | the texture format, palette quantization, and the black-vs-transparent trap |
| [`mppcdiscs/hello/README.md`](mppcdiscs/hello/README.md) | the sample disc, annotated |
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
- **Not there yet** — semi-transparency and blending, ADPCM / pitch / reverb,
  gyro and trackpads, a contracted RAM budget (video and sound RAM are enforced;
  main RAM is not), the 256×224 display mode, Lua discs.

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
  | the disc's own id | each packaged disc | `hello::rv_dmain`, `mygame::rv_dmain`, … |
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
- Machine-generated code carries a `NEUROSLOP` banner or `NEUROSLOP-BEGIN/END`
  markers. It has not been reviewed by a human.

## Requirements

SDL3 (used from the system if installed, otherwise built from source on the
first configure), CMake 3.24+, Ninja, and a C++23 compiler. The tools
additionally shells out to `cmake` and `ninja` at run time to compile a disc,
and downloads `stb_image.h` into its own build directory.
