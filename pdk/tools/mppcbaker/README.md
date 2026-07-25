# `pdk/tools/` — offline devkit tools

Everything here runs **on the developer's machine at build time**, not on the
console. No tool links against `src/` or sees a console header: the only shared
truth is the contract in `pdk/include/pdk/cv/rv_texture.hpp`.

```
cmake -S tools -B build-tools && cmake --build build-tools
```

(or `add_subdirectory(tools)` from the top-level `CMakeLists.txt` — the file works
in both modes).

---

## `mppcbaker` — PNG → `.mppcbaker`

The console does not decode image formats and never will. A disc hands
`rv_cv::video_asset_write()` **finished texels**, exactly as it would on real
devkit hardware. So turning a picture into texels is a build-step job, and
`mppcbaker` is what does it.

```
mppcbaker <input.png> <output.mppcbaker> --format idx4|idx8|direct15
        [--transparent-key RRGGBB]
```

| Option | Meaning |
| --- | --- |
| `--format idx4` | 4-bit palette index, 16-entry palette (`RV_TEXFMT_IDX4`) |
| `--format idx8` | 8-bit palette index, 256-entry palette (`RV_TEXFMT_IDX8`) |
| `--format direct15` | 15-bit direct color + STP, no palette (`RV_TEXFMT_DIRECT15`) |
| `--transparent-key RRGGBB` | which source color counts as a hole (hexadecimal, `#` optional) |

The PNG alpha channel is honoured **always**, regardless of `--transparent-key`:
`alpha < 128` → the pixel is transparent. A PNG without alpha is fully opaque.

Any error gets a readable message on `stderr` and exit code `1`.

### Examples

```sh
# sprite with alpha, 16 colors
mppcbaker assets/protagonist_tex.png build/protagonist_tex.mppcbaker --format idx4

# sprite without alpha, where the hole is marked with hot pink
mppcbaker assets/hud.png build/hud.mppcbaker --format idx8 --transparent-key FF00FF

# opaque background, full color
mppcbaker assets/sky.png build/sky.mppcbaker --format direct15
```

---

## `.mppcbaker` file layout

Everything is little-endian. The file is a header, then the palette, then the
texels; both the palette and the texels are stored **exactly in the form they
will be handed to `video_asset_write`**, so the disc never has to repack
anything.

### Header — 16 bytes

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| `0` | 4 | `magic` | ASCII `"MPTX"` |
| `4` | 2 | `version` | `1` |
| `6` | 2 | `format` | an `rv_texfmt` value: `4`, `8` or `15` |
| `8` | 2 | `width` | texels per row |
| `10` | 2 | `height` | rows |
| `12` | 2 | `palette_count` | palette entries: `16` for IDX4, `256` for IDX8, `0` for DIRECT15 |
| `14` | 2 | `reserved` | `0` (checking it for zero is not required, but not forbidden either) |

The header is 16 bytes precisely so that the palette behind it is 2-byte
aligned: the disc can point a `const uint16_t*` straight into the buffer it read.

### Palette — `palette_count * 2` bytes, at offset `16`

`palette_count` consecutive `uint16` entries. An entry has the same format as a
DIRECT15 texel (see below). For `direct15` the section is absent entirely
(`palette_count == 0`).

The palette is always written at **full length** (16 or 256 entries) even when
the image uses fewer colors: the disc does not need to know how many are really
occupied — it uploads the palette as a single `rv_texture` of fixed shape. The
unused tail is filled with `0000h`, i.e. "do not draw": an index that should
never occur draws a hole rather than a random color.

If the source has even one transparent pixel, **entry 0 is reserved** for
`0000h` and the colors occupy entries `1..palette_count-1`. With no transparent
pixels, all `palette_count` entries go to colors.

### Texels — from offset `16 + palette_count * 2` to the end of the file

| Format | Bytes per row (stride) | Total |
| --- | --- | --- |
| `IDX4` | `(width + 1) / 2` | `stride * height` |
| `IDX8` | `width` | `width * height` |
| `DIRECT15` | `width * 2` | `width * height * 2` |

Rows run top to bottom, texels left to right; there is no row padding beyond the
stride given above.

### The 16-bit value (DIRECT15 texel and palette entry)

```
 bit  15 14        10 9         5 4         0
     ┌───┬────────────┬───────────┬───────────┐
     │STP│    blue    │   green   │    red    │
     └───┴────────────┴───────────┴───────────┘
```

5 bits per channel, `0..31`. Bit 15 is the semi-transparency flag (STP);
`mppcbaker` always writes `0` there, because the semi-transparent blending modes
are still deferred on the console side, and an STP set in advance would silently
change how a texture looks once they land.

**`0000h` = FULLY TRANSPARENT**, the pixel is not drawn at all.

---

## Trap #1: there is no such thing as opaque black

Since `0000h` marks a hole, an opaque pixel **must never** encode to `0000h`.
Naive quantization sends any very dark color there — and the shadows in the image
turn into see-through holes. This is the classic PSX trap: you cannot spot it by
eye in an editor, it only shows up once the sprite is drawn over a background.

`mppcbaker` handles it explicitly: **any opaque color that packs to `0000h` is
nudged to `0001h`** — the darkest red, one 1/31 step of red away, which is below
the noise floor of the console's own dithering. The rule lives in one place
(`pack_opaque`), so it applies to DIRECT15 texels and palette entries alike.

The practical consequence for an artist: black in a texture is always "almost
black". If you need true black on screen, that is the job of the background or
the frame clear, not of a texture. `8000h` is black with the STP flag, and it is
not "opaque black" either.

## Trap #2: nibble order in IDX4

In `IDX4` two texels share a byte, and **the low nibble is the LEFT texel**:

```
byte:  [ high nibble | low nibble ]
                ↑            ↑
           texel x+1     texel x
```

This is the PSX order, and the console side is pinned to it too. Swapped nibbles
produce an image that looks *almost* right (pixel pairs trade places) — the worst
kind of bug, because it gets mistaken for a scaling artifact.

For an odd `width` the last byte of a row is padded with a zero high nibble, so
every row starts on a byte boundary and the stride is `(width + 1) / 2`.

---

## Quantization

The indexed formats use **median cut (Heckbert 1982)** followed by a few
**Lloyd** iterations. The reasoning is spelled out in the `// THEOREM:` comments
in `mppcbaker.cpp`; in short:

- **Why median cut and not k-means.** It is deterministic — the same PNG yields a
  byte-identical file, and without that the build cache and diffs of committed
  assets are meaningless. It has no initialization problem, so it cannot waste a
  palette slot on an empty cluster. And splitting at the median makes it
  population-driven: a color covering half the image gets half the palette.
- **Why Lloyd on top.** Median cut's boxes are axis-aligned, so a diagonal
  gradient through the color cube gets cut coarsely. A fixed, small number of
  Lloyd passes fixes that while staying deterministic and bounded.
- **Quantization happens directly in 5-bit space.** Palette entries physically do
  not hold more than 5 bits per channel, so measuring distances in 8 bits means
  optimizing precision the console throws away immediately.
- **The metric is luminance-weighted** (`3·dr² + 6·dg² + 1·db²`): the eye
  resolves green markedly better than blue. It is integer, so the search is exact
  and reproducible across compilers.
- Transparent pixels **take no part** in the statistics: whatever RGB sits under
  the alpha is usually junk, and it must not pull the palette towards itself.

If there turn out to be more distinct colors than slots, the tool prints a
warning on `stderr` (this is not an error — the exit code stays `0`).

---

## How a disc reads `.mppcbaker`

Texels and palette are already in their final form in the file, so the loader is
a read, a header check and two `video_asset_write` calls.

1. Read the whole file into main memory (`rv_cd`).
2. Check `magic == "MPTX"` and `version == 1`; otherwise fail, do not guess.
3. Parse the header: `format`, `width`, `height`, `palette_count`.
4. Compute the offsets:
   - palette: `16`, length `palette_count * 2` bytes;
   - texels: `16 + palette_count * 2`, length
     `height * ((width + 1) / 2)` for IDX4, `width * height` for IDX8,
     `width * height * 2` for DIRECT15.
   The sum has to match the file size — if it does not, the file is corrupt.
5. If `palette_count != 0`, upload the palette:
   ```cpp
   rv_texture clut{};
   clut.format = RV_TEXFMT_DIRECT15;      // a palette is described as DIRECT15
   clut.data   = bytes + 16;
   clut.size   = palette_count * 2;
   clut.width  = palette_count;           // 16 or 256
   clut.height = 1;
   const int64_t addr_palette = cv.video_asset_malloc(clut.size);
   cv.video_asset_write(addr_palette, clut);
   ```
6. Upload the texels:
   ```cpp
   rv_texture tex{};
   tex.format = static_cast<rv_texfmt>(header.format);  // 4 / 8 / 15
   tex.data   = bytes + 16 + palette_count * 2;
   tex.size   = <length from step 4>;
   tex.width  = width;
   tex.height = height;
   const int64_t addr_texture = cv.video_asset_malloc(tex.size);
   cv.video_asset_write(addr_texture, tex);
   ```
7. Free your own buffer (the bytes are copied inside the call) and keep
   `addr_texture` / `addr_palette` — they go into `rv_polygon::addr_texture` and
   `rv_polygon::addr_palette` from here on. For `direct15` no `addr_palette` is
   needed: that format ignores the palette.

Everything else — transparency, the black trap, the nibble order — is already
baked into the bytes of the file. The disc has nothing left to adjust.
