# 3dmppc — Specification

A fantasy console in the spirit of the Sony PlayStation (PSX). This document has
two parts:

- **Target spec** — what we actually implement in `3dmppc`.
- **Reference: real PSX** — the hardware that inspired it (numbers for comparison,
  not requirements).

---

## Target Spec

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

### Audio (`rv_ca`)
| Query | Reference answer |
| --- | --- |
| `voice_count` | 24 |
| `sound_memory_size` | 512 KB |

- Sample-based (raw PCM for now, ADPCM deferred), 16-bit, stereo

### Input (`rv_cio`)
| Query | Reference answer |
| --- | --- |
| `iport_count` | 2 |

### Save (`rv_cm`)
| Query | Reference answer |
| --- | --- |
| `card_slots` | 16 |
| `card_slot_size` | 8 KB (total 128 KB) |

### Memory
- **RAM:** 2 MB (main heap; not yet contracted — no RAM controller in the PDK)

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
