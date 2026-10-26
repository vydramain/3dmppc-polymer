# minimp3

Single-header MP3 decoder. Used by `pdk/tools/mppcmusbaker` to decode an artist's
MP3 into the console's S16LE mono PCM at bake time.

| | |
| --- | --- |
| Upstream | https://github.com/lieff/minimp3 |
| Commit | `ea99364f61c14656440e8d77e9c233ccf3124633` (master, committed 2026-07-27) |
| Taken | 2026-10-18 |
| Licence | CC0 — see `LICENSE` |

## Files

| File | SHA-256 |
| --- | --- |
| `minimp3.h` | `57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad` |
| `LICENSE` | `6a1ee543e5282cd9061881edf462e6fdab181f328da71fc2c9a6950a80e94d01` |

Unmodified. No patches applied.

## Why it is vendored

The disc keeps its music as MP3; the baker turns it into the console's PCM at build
time. The console never decodes MP3 itself, so the decoder is needed only by the
tool, and `src/` links nothing from here.
