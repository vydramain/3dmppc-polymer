# win-55-ui icons

Six 30x30 icons. Used by `editor/` in its widget catalog and toolbars.

| | |
| --- | --- |
| Upstream | https://github.com/OmskGameJam/win-55-ui |
| Commit | `7fe2b31a76f9eeff31f12e2fbdc41710146c3c6f` |
| Path upstream | `public/win-55-ui/icons/` |
| Taken | 2026-09-23 |
| Licence | MIT - see `LICENSE.md` |

The icons were redrawn by the project's author in the manner of Windows 95;
they are not copies of Microsoft artwork.

## Files

| File | SHA-256 |
| --- | --- |
| `broken-image.png` | `79ae9da6adc54954185a70db2b24076ac84c5a1198624b9ab96eb511388c4225` |
| `calendar.png` | `fc3312495e520b2fd065d4dc803ea4a08a5f42c754e25f1e03f1f5502fea54cb` |
| `folder.png` | `6bf52d2810a9669235b89c111cb4b6d51e2806457724132cf28258b5702c058c` |
| `gizmo.png` | `071c01488059eaac4742edf963b3c9c2306d9eb270b9bb6163f2c4a842b4c711` |
| `neko.png` | `b5bbdee445531805c3f1c85ab8d9977c44143d75bceba5d01d1ed4f3a7d42528` |
| `program.png` | `f4dad6146f1d98162b9154017deb000fa29d78fdcec8ec10603342aab10210fd` |

Unmodified. No patches applied.

## What is deliberately not taken

Only the icons. The repository's `Standard-*` fonts are rasterized from
Liberation Sans 1.x (GPLv2 with the font exception) and Noto Sans JP, so the
repository's MIT licence does not cover them; the emoji `000`-`BA1` are NTT
DoCoMo / KDDI artwork excluded from that licence by the repository itself. The
9-patch borders and cursors are not needed: the editor draws its own bevels.

## Updating

Copy the same files from a newer commit and update the commit, date and hashes
above. Take nothing else from that repository without checking its licence
first.
