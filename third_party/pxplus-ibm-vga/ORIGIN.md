# PxPlus IBM VGA 9x16

The IBM VGA 9x16 text-mode font as an outline TTF. Used by `editor/` as its only
font, for the interface and for code.

| | |
| --- | --- |
| Upstream | http://int10h.org - The Ultimate Oldschool PC Font Pack |
| Author | VileR (outline version, 2020) |
| Version | `v2.2-2020-11` (the font's own name table) |
| Taken | 2026-09-24 |
| Licence | CC BY-SA 4.0 - see `LICENSE` |

Attribution: "PxPlus IBM VGA 9x16" by VileR, int10h.org, licensed under
Creative Commons Attribution-ShareAlike 4.0 International
(https://creativecommons.org/licenses/by-sa/4.0/).

## Files

| File | SHA-256 |
| --- | --- |
| `PxPlus_IBM_VGA_9x16.ttf` | `632f4409b32af37e5bbd5adc683c0f4a53d4a7b4707943c9f52a1eb3bfab14e3` |
| `LICENSE` | `9348ddfd44da5a127c59141981954746a860ec8e03e0412cf3af7134af0f97e2` |

Unmodified. No patches applied.

## The adaptation in pdklib

`pdk/lib/include/pdklib/rv_font/rv_font_cyrillic.hpp` holds the Cyrillic block
of the discs' 5x7 font, redrawn after this font's Cyrillic glyphs. It is an
adaptation and carries the same licence, CC BY-SA 4.0. A disc that builds its
atlas with `rv_font_block_cyrillic` ships that table and gives the attribution
above. The ASCII atlas does not use it, so a disc without the block, and the
console itself, contain none of it.
