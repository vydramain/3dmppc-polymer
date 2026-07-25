# 3dmppc — Specification

A fantasy console in the spirit of the Sony PlayStation (PSX). This document has
two parts:

- **Target spec** — what we actually implement in `3dmppc`.
- **Reference: real PSX** — the hardware that inspired it (numbers for comparison,
  not requirements).

---

## Target Spec

**Every number below is a *virtual* budget the console imposes on itself.** None
of it is a property of the machine `3dmppc` runs on: the host has gigabytes of
RAM and a screen far larger than 320×240, and the console hands a disc none of
it. The figures describe the fantasy hardware, not the hardware underneath it.

The budget is not decoration — wherever the contract can hold the line, it does,
and that is what makes a game written for this machine actually written *for*
it rather than merely on it:

- the video-RAM pool answers `RV_ERR_NOMEM` once its 1 MB is handed out, and the
  sound-RAM pool the same for its 512 KB (`src/rv_infra/rv_pcpool.hpp`) — through
  the contract a disc has no way to fall back on the host allocator;
- a frame rejects the primitive past `frame_capacity` with `RV_ERR_NOMEM`, and a
  texture over `texture_max_width/height` is refused at upload
  (`src/rv_pconsole/cv/rv_pccv.cpp`);
- `card_write` refuses a blob larger than one slot;
- `mppcburner` re-checks the same texture and video-memory budget at pack time,
  so an overflow is caught on the author's desk instead of on a loading screen.

Where the budget is **not** enforced, said plainly rather than implied:

- **Main RAM (2 MB) is not in the contract at all.** There is no RAM controller;
  a disc allocates through the host and nothing counts the bytes. That figure is
  a target the disc author honours voluntarily, and nothing else.
- **The medium has no size ceiling.** The drive mounts whatever it is given; a
  disc may be arbitrarily large.

The PDK carries **no numbers** — a disc queries the console at `disc_initialize`
and validates its baked assumptions (see `pdk/README.md`, "Video"). The values
below are the **reference console's answers** (`rv_pconsole` defaults):

### Display / video (`rv_cv`)
| Query | Reference answer |
| --- | --- |
| `screen_width` × `screen_height` | `320×240` (secondary mode `256×224` planned) |
| `texture_max_width` × `texture_max_height` | `256×256` |
| `video_memory_size` | 1 MB |
| `frame_capacity` | 4096 primitives/frame |
| *(hidden by design)* | 1024 ordering-table buckets |

- **Color:** 16-bit + dithering; texel formats 4-bit / 8-bit paletted +
  15-bit direct (PSX transparency rules), no filtering
- **Texture mapping is AFFINE, on purpose.** `rv_vertex` carries no `w`, so uv is
  interpolated in screen space and perspective correction is not merely skipped —
  it is unreconstructible on the disc side of the contract. Textures visibly swim
  on polygons receding into depth, and a quad breaks along its split diagonal.
  That is the PSX signature, not a defect.
- **IDX4 packing:** two texels per byte, **low nibble = left texel**. Rows are
  padded to a whole byte, so an odd width costs a nibble.
- **Ordering-table depth** is quantized into 1024 buckets over the console's
  depth window; two different depth values can share a bucket, where submission
  order decides. `RV_PIPELINE_BUFFER_CONFIG_TYPE_Z` resolves exactly that
  residue per pixel.

### Audio (`rv_ca`)
| Query | Reference answer |
| --- | --- |
| `voice_count` | 24 |
| `sound_memory_size` | 512 KB |

- Sample-based (raw PCM for now, ADPCM deferred), 16-bit, stereo

The contract leaves the sample encoding and the envelope units implementation-
defined; the reference console fixes them as follows, and a disc that assumes
otherwise will sound wrong rather than fail:

| Detail | Reference answer |
| --- | --- |
| Sound-RAM encoding | raw **S16LE mono** at 44100 Hz |
| Output | stereo S16; a voice is placed by its own `volume_l` / `volume_r` |
| `ar` / `dr` / `sr` / `rr` | **milliseconds**; `sr == 0` holds sustain indefinitely |
| `sl`, `volume`, `volume_l`, `volume_r` | `0..32767`, where 32767 is unity; negatives read as silence |
| Envelope shape | **linear** ramps (the PSX SPU also has exponential ones — deliberate MVP simplification) |
| Mixing | voices sum in `int32` and **clip** at the `int16` rails, they are not divided by voice count |

A mono voice placed by per-channel volumes is the PSX SPU model, and is exactly
why `rv_sample` carries no format fields at all.

### Input (`rv_cio`)
| Query | Reference answer |
| --- | --- |
| `iport_count` | 2 |

### Save (`rv_cm`)
| Query | Reference answer |
| --- | --- |
| `card_slots` | 16 |
| `card_slot_size` | 8 KB (total 128 KB) |

- The card is backed by a single 131232-byte image (`memcard.mppccard` by
  default). It is created lazily on the first write, and a file whose geometry
  or magic does not match is **refused, never reformatted** — those are somebody
  else's saves.
- `card_write` is atomic: the new image goes to a temporary file in the same
  directory, is `fsync`ed, and replaces the old one with `rename`. That is what
  backs the contract's promise that a failed write leaves the old slot intact.

### Disc medium (`rv_cd`)

- The medium is a **directory** today (`--disc PATH`); a `.mppcdisc` archive
  takes its place behind the same interface at the packaging stage.
- A resource name is a name, never a path: separators and `..` are rejected
  before anything reaches the filesystem.

### Memory
- **RAM:** 2 MB (main heap; not yet contracted — there is no RAM controller in
  the PDK, so unlike video and sound RAM nothing actually enforces this figure)

### Game package
- `.mppcdisc` — an *mppc polymer disc*: the console name (`mppc`) plus the
  PSX-era optical medium (`disc`). This is the one package a game ships as.

---

## Reference: Real PSX

| Subsystem | Spec                                             |
| --------- | ------------------------------------------------ |
| CPU       | LSI CoreWare 33.9 MHz, 32-bit RISC (MIPS R3000A) |
| GPU       | Custom Sony Graphics Engine                      |
| Polygons  | 180,000 textured polygons/sec                    |
| Palette   | up to 16.7M colors (24-bit true color)           |
| RAM       | 2 MB main RAM + 1 MB VRAM                         |
| Storage   | 2× CD-ROM (300 KB/s), 660 MB                      |
| Audio     | Custom SPU, 24 ADPCM channels up to 44.1 kHz     |

---

## Target vs Reference

Deliberate differences between `3dmppc` and the original — worth tracking so they
don't drift by accident:

- **Color:** target is 16-bit + dithering; the PSX reference lists 24-bit true
  color. We keep 16-bit as the primary frame format.
- **CPU / GPU:** everything is software on day one (CPU rasterizer). The PSX
  hardware numbers are only a guideline for the polygon budget.
- **Storage vs package:** instead of a CD-ROM, games ship as a single
  `.mppcdisc` package.
