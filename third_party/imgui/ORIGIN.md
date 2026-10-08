# Dear ImGui

Immediate-mode GUI library. Used by `editor/` - the 3dmppc-editor window, its
input, its widgets - through the SDL3 platform backend and the SDL_Renderer
backend.

| | |
| --- | --- |
| Upstream | https://github.com/ocornut/imgui |
| Tag | `v1.92.9b` (branch `master`, not `docking`) |
| Commit | `f1cc2ae15e53a861a874c3034aae6798fde194ab` |
| Taken | 2026-09-23 |
| Licence | MIT - see `LICENSE.txt` |

## Files

Only what the editor compiles: the core, its bundled stb headers, and two
backends. `imgui_demo.cpp`, `examples/`, `misc/` and the other backends are left
upstream.

| File | SHA-256 |
| --- | --- |
| `backends/imgui_impl_sdl3.cpp` | `306060dffa276b574f0541194e8e079c1685c29c1095602a3972de0bb23171e7` |
| `backends/imgui_impl_sdl3.h` | `fa184f17d59d31e986df2e827ef964d7b3c28bcba7789643a4aaaed344cb868e` |
| `backends/imgui_impl_sdlrenderer3.cpp` | `8cb065251a41dcccfc181810b5fec979498c6ffe43fa389a3c86d4d5506e3ca5` |
| `backends/imgui_impl_sdlrenderer3.h` | `553cd6cd3efb8b887954e7f2d804a79538651a5332ef68c812effe27c5c577bc` |
| `imconfig.h` | `5755e1b8d6ab0d7811a9d7cac0f509878b51fc5ee77e09bd0f235bcb414971e7` |
| `imgui.cpp` | `5a1e5128c0305f50b6556a77f71fde35836505883b74c1d6ae44723243f1e8de` |
| `imgui_draw.cpp` | `83d30419a8e06a5f0a8692ee6de186f7ecfddecdc33769e76244837e2975b400` |
| `imgui.h` | `0d8db1045db01d908853adfd26ae07c5bc5ab4789d4515f6ea34234a69ade0ca` |
| `imgui_internal.h` | `efba9bccc971cc49ec3da3168968ba412c7ea89745c104a7f5c004c8349bc71f` |
| `imgui_tables.cpp` | `b5deabe5b569ab712c11b6556562a646bf88327320fc60a451071b2a36498d72` |
| `imgui_widgets.cpp` | `a8a0b2f65caa5711467d9e3ca4028fded48329ea65e11ec92a968293dc2aa231` |
| `imstb_rectpack.h` | `889b396795202d1457560a797a7242e96f6f132d4b88ca2d69be58bf05e1771f` |
| `imstb_textedit.h` | `a985f5fa0ed97353d493b497961e9eef52082edcd045cf6954b69990ec9d0741` |
| `imstb_truetype.h` | `c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528` |

Unmodified. No patches applied. Build options go through `IMGUI_USER_CONFIG`
from `editor/`, not through edits to `imconfig.h`.

## Why this one

ImGui has no build system on purpose: it is a handful of `.cpp` files the
consumer compiles, which is exactly how a vendored dependency wants to look.
It owns the parts of a GUI that are pure plumbing - ids, focus, hover, text
editing, clipboard, tables, popups, keyboard navigation - and leaves how things
look to the editor, which draws its own bevels on top.

## Why it is not in the console

The console has no GUI and never will. Only `editor/` compiles these files, and
the editor is built only by a development configuration
(`-D3DMPPC_DEVTOOLS=ON`) or on its own.

## Updating

Take a new tag, copy the same file list, update the tag, commit, date and
hashes above, and rebuild the editor. Anything the editor calls from
`imgui_internal.h` is not a stable API and is the first place to look when an
update breaks the build.
