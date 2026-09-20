# example-lua — Example mppcdisc with Lua scripting

The Lua sibling of [`example-cpp/`](../example-cpp/): same disc lifecycle, but
every hook is written in Lua and reached through `rv_cl` — the console's
script contract
([`pdk/include/pdk/cl/rv_cl.h`](../../pdk/include/pdk/cl/rv_cl.h)) — instead
of linked directly into `disc.so`. It exists to prove that path end to end: a
script that reads real hardware state back through the console and draws a
frame with it, not just a "hello world" print.

## The two halves

```
example-lua/
  disc.toml               manifest — declares [scripts] and [budget.pccl]
  src/example-lua.cpp      the disc: one pdklib macro that forwards every hook into Lua
  scripts/example-lua.lua  the entry chunk: mirrors the disc's hook shape
```

- **`src/example-lua.cpp`** is one line, `RV_MPPC_LUA_DISC_DEF("example-lua")`
  from `pdklib/rv_dscript/rv_dscript.hpp`. The macro defines the disc class
  and plants it with `RV_MPPC_DISC_ENTRY_DEF`, so this is still a real disc,
  `dlopen`ed like any other. Its `disc_initialize` raises the entry chunk with
  `rv_cl_script_entry()`, and from then on every lifecycle hook
  (`disc_initialize`, `frame_update`, `frame_render`, `disc_shutdown`) is one
  `rv_cl_script_call()` into the same-named Lua function, handed the organizer
  (`rv_pdko*`) as light userdata. There is no fallback: a disc that declares no
  lua machine gets the null one, `rv_cl_script_entry()` answers
  `RV_ERR_INVAL`, and the disc refuses to start.
- **`scripts/example-lua.lua`** is compiled by `mppcburner` into
  `example-lua.luac` in an archive (an `--unpacked` directory symlinks the
  `.lua` source instead) and is the chunk named above. It returns a module table
  (`M.disc_initialize`, `M.frame_update`, ...) instead of touching `_G`, and
  reaches hardware through the global `pdk` table — `pdk.cv(o)` for the video
  controller, then `pdk.cv_screen_width`, `pdk.cv_frame_configure`,
  `pdk.cv_frame_put`. It reads the real
  screen size back from the console and logs it, then fills the frame with
  one triangle every `frame_render` — `FLAT_COLOURED` contrasts with
  `SAMPLE_TEXTURE` (vertex colour vs. texel), not with shading, so its three
  differently-coloured vertices Gouraud-interpolate into a red→green→blue
  gradient — the same configure-then-`cv_frame_put` shape `example-cpp.cpp`
  uses for its own
  sprites, built from a `rv_primitive` the script constructs itself with
  `pdk.new`. The C++ side flushes the frame right after the hook returns: the
  macro's `frame_render()` is one `rv_cl_script_call()` followed by one
  `rv_cv_frame_flush()`, and a failed call skips the flush.

## Asking for a texture

Past the triangle, the script also draws `example-sprite.mppctex` from the
drive. It never acquires or releases the texture - it only names it. Every
`frame_render`, it queries the drive for that name's current address, palette
address, width and height with `pdk.cd_resource_addr`, `cd_resource_palette_addr`,
`cd_resource_width`, and `cd_resource_height`, each passed `pdk.CD_RESOURCE_TEXTURE`
as the kind it is asking about. The drive makes the texture
resident the first time any of those names it, keeps it resident and
refreshes it in place on a dev reload - the name is stable, the addresses
change, and the game picks the new texture up by asking for the address fresh
each draw - and frees it itself when the disc unloads.

## Code != state

The console keeps one persistent table alive for the whole run, independent
of whatever chunk is currently loaded, and hands it to `M.attach(state)` —
once at boot, right after the entry chunk is raised, and again after every
successful reload of that chunk's code. A chunk local or a field of `M` dies
with the code; only a field of `state` survives a reload, so that is where
this script keeps the screen size and a frame counter it increments once per
`frame_update`; `M.state_shape` declares both of them, and the console checks
the live state against it before `attach` runs. Boot the `--unpacked` directory with `--dev`, change `frame_render`'s
colours in `scripts/example-lua.lua` and send `reload entry` - the picture
changes but the counter keeps climbing instead of resetting to 0, which is
the whole point: the code changed, the state did not. This `attach` accepts
every state the shape check lets through; returning `false` and a reason is
how a chunk refuses a layout it has no migration for. Either way a reload is
atomic in code - either the new chunk accepts the state and takes over, or it
is refused and the old chunk keeps running untouched. Neither a Lua function
nor a coroutine is ever stored in `state`: either would keep the old chunk's
bytecode alive after a reload was supposed to have replaced it.

A per-frame script failure does not disable scripting for the rest of the run
either: the macro keeps calling `frame_update`/`frame_render` every frame, and
the machine logs every failed call to stderr. With `--dev` the console stops
the loop at the first failure instead and reports it as a `0 event=script_error`
line, so a reload can fix the script before the next frame runs.

`pdk` is not a wrapper: it is a LuaJIT FFI table whose `__index` resolves
`pdk.cv_frame_put` to `ffi.C.rv_cv_frame_put` the first time it is touched —
the exact same exported symbol `example-cpp.cpp` calls as a plain C function.
Confirm it yourself:

```sh
$ nm -D --defined-only build/pconsole/3dmppc | grep rv_cv_frame_put
00000000000f64b0 T rv_cv_frame_put
```

That symbol is exported once, by the console binary; both a C++ disc and a
Lua script land on it.

## `[budget.pccl]`

```toml
[budget.pccl]
script_memory_size = 262144
script_entry = "example-lua.luac"
```

`script_memory_size` is the byte ceiling for the *whole* Lua machine this
disc gets — LuaJIT's own bootstrap (base/string/math/table plus the `pdk`
bridge) and everything the script itself allocates. The console's `lua_Alloc`
refuses growth past it; there is no other RAM budget backing scripts.
`script_entry` names the asset `mppcburner` compiled `scripts/*.lua` into —
the console resolves and verifies it against the packaged disc while loading
the manifest, so `rv_cl_script_entry()` never takes a name from the disc
itself, only asks for "the one you already checked".

## Burning and running

```sh
./pdk/tools/build/mppcburner/mppcburner build mppcdiscs/example-lua -o build/example-lua.mppcdisc \
    --baker pdk/tools/build/mppcbaker/mppcbaker
./build/pconsole/3dmppc build/example-lua.mppcdisc
```

Watch stderr for `pccl: lua machine up, 262144 byte(s) budgeted` on load,
followed by:

```
Hello from example lua!
example-lua: screen is 320x240 (read through pdk)
```

`print()` is routed into the console's stderr logger, not stdout, so both
lines land there. The second one is the proof: it only prints once
`pdk.cv_screen_width` has round-tripped into the console's real video
controller and back.
