# 3dmppc — PSX-like fantasy console

A tiny C++23 fantasy console in the spirit of the PlayStation. It rasterizes
entirely on the CPU into a 320×240 framebuffer of 15-bit colour and presents it
through SDL3 with crisp integer scaling.

The three trees, and the boundary between them, are the whole point:

| Tree | What it is |
| --- | --- |
| [`pdk/`](pdk/) | the **contract** — the devkit headers both sides depend on |
| `src/` | the **console** — the concrete machine that implements the contract |
| [`mppcdiscs/`](mppcdiscs/) | the **discs** — games, which depend on `pdk/` only |

Target hardware fantasy lives in [`docs/platform/specs.md`](docs/platform/specs.md);
the console-vs-disc split is [`docs/README.md`](docs/README.md).

## Where it stands

- **PDK contract** — closed. Video, audio, drive, memory card and I/O are all
  specified, with a shared kernel-style error convention (`rv_err`).
- **Video (`rv_cv`)** — live: video RAM pool, ordering table, a rasterizer for
  lines / sprites / triangles / quads with Gouraud interpolation and 4×4 ordered
  dithering. Texture sampling is the next step.
- **Input (`rv_cio`)** — live: gamepads through SDL, with the keyboard overlaid
  on port 0.
- **Drive (`rv_cd`), memory card (`rv_cm`), audio (`rv_ca`)** — still stubs that
  answer `RV_ERR_IO`.
- **The disc** — `src/rv_dmain` is a skeleton "light show" that proves the frame
  pipeline end to end. The real game (Solidmaid) is designed in
  [`docs/mppcdisc/solid/`](docs/mppcdisc/solid/) and not written yet.

## Layout

```
pdk/include/pdk/     the contract: rv_pdko (facade) + rv_de (disc entry)
  cv/ ca/ cd/ cm/ cio/   one controller per subsystem
src/
  main.cpp           argv -> console configuration -> boot
  rv_pconsole/       the machine
    rv_pconsole.*      composition root + the frame loop
    rv_pchost.*        the only file that knows SDL exists
    cv/                video: framebuffer, VRAM pool, ordering table, rasterizer
    ca/ cd/ cm/ cio/   the other four controllers
  rv_dmain/          the skeleton disc (builds against pdk/ ONLY)
  rv_infra/          console-internal odds and ends (the logger)
mppcdiscs/           disc library — the games the console loads
docs/README.md       console vs disc — read this first
docs/platform/       the console: hardware spec, disc format
docs/mppcdisc/solid/ the reference game (Solidmaid) design
```

## Build & run

SDL3 is used from the system if installed (e.g. `libsdl3-dev`), otherwise built
from source on the first configure — that one needs a network connection.

```sh
cmake -S . -B build -G Ninja
cmake --build build

./build/3dmppc                        # Esc, Option/Start, or close the window to quit
./build/3dmppc --scale 4              # window magnification over native 320×240
./build/3dmppc --headless --frames 120  # smoke test: no window, bounded run
./build/3dmppc --fixed-step           # feed the disc a fixed 1/60 dt
```

## Conventions

- Left-handed math: `+x` right, `+y` up, `+z` forward.
- Every call across the PDK returns `>= 0` on success and a negative `rv_err`
  on failure — callers test with `if (rc < 0)`.
- Design decisions are tagged in the source: `grep -rn "PATTERN:\|THEOREM:" src/`
  maps every pattern and algorithm to the line that implements it.
- Machine-generated code carries a `NEUROSLOP` banner or `NEUROSLOP-BEGIN/END`
  markers. It has not been reviewed by a human.

## Next steps

- **Texture sampling** — `RV_PRIMITIVE_FILL_MODE_SAMPLE_TEXTURE` currently draws
  flat; the VRAM pool already stores the texels and the palette.
- **The disc drive (`rv_cd`)**, then the memory card and the sound chip.
- **Packaging** — a disc becomes its own `.so` inside a single `.mppcdisc`,
  loaded at runtime instead of linked in. Design:
  [`docs/platform/disc-loading.md`](docs/platform/disc-loading.md).
- **Lua** scripting for script-kind discs, once the native path is real.
