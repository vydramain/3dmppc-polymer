# stb_image

Single-header image decoder. Used by `pdk/tools/mppcbaker` to read the artist's
PNG before quantizing it into console texels.

| | |
| --- | --- |
| Upstream | https://github.com/nothings/stb |
| Commit | `f0569113c93ad095470c54bf34a17b36646bbbb5` |
| Version | `stb_image` v2.30 (the version string inside the header) |
| Taken | 2026-07-25 |
| Licence | public domain (Unlicense) **or** MIT, at your option — see `LICENSE` |

## Files

| File | SHA-256 |
| --- | --- |
| `stb_image.h` | `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3` |

Unmodified. No patches applied.

## Why this one

Decoding PNG properly means implementing DEFLATE, the filter types, interlacing
and the colour-type matrix — a week of work to get a picture into memory, which
is not what this project is about. `stb_image` is a single self-contained header
in the public domain, which makes it the cheapest possible dependency: one file
to read, one file to audit, nothing to install.

## Why it is not in the console

The console never decodes an image format. A disc uploads finished texels
through `rv_cv::video_asset_write`, and turning a PNG into those texels happens
once, offline, on the developer's machine. That is why this lives beside the
tools and why `src/` links nothing from here.

## Updating

Rarely needed — `stb_image` is stable and this project uses a fraction of it.
When it is: download the new file, replace it, update the commit, version, date
and hash above, and rerun `mppcbaker` over the disc assets to confirm the output
is byte-identical (or to see deliberately what changed). A decoder that changes
silently changes shipped art silently.
