# 3dmppc — Specification

A fantasy console in the spirit of the Sony PlayStation (PSX). This document has
two parts:

- **Target spec** — what we actually implement in `3dmppc`.
- **Reference: real PSX** — the hardware that inspired it (numbers for comparison,
  not requirements).

---

## Target Spec

### Display
- **Resolution:** two modes — `320×240` or `256×224`
- **Color:** 16-bit + dithering
- **Textures:** 4-bit / 8-bit paletted, no filtering

### Memory
- **RAM:** 2 MB (main heap)
- **VRAM:** 1 MB (video heap)

### Audio
- Sample-based, ADPCM-like
- 16-bit, 24 voices, stereo

### Input
- 1–2 gamepads

### Save
- Memory card, e.g. 128 KB

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
