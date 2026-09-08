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
  src/example-lua.cpp      the disc: implements rv_de, forwards every hook into Lua
  scripts/example-lua.lua  the entry chunk: mirrors the disc's hook shape
```

- **`src/example-lua.cpp`** is what `RV_MPPC_DISC_ENTRY_DEF` actually plants —
  it is a real disc, `dlopen`ed like any other. It raises the entry chunk once
  with `rv_cl_script_entry()`, hands it the `rv_cv`/`rv_ca`/`rv_cio` pointers
  as light userdata, and from then on every lifecycle hook
  (`disc_initialize`, `frame_update`, `frame_render`, `disc_shutdown`) is one
  `rv_cl_script_call()` into the same-named Lua function. There is no
  fallback: if the console gives it a null `rv_cl*` (scripting off), it
  refuses to start.
- **`scripts/example-lua.lua`** is compiled by `mppcburner` into
  `example-lua.luac` and is the chunk named above. It returns a module table
  (`M.disc_initialize`, `M.frame_update`, ...) instead of touching `_G`, and
  reaches hardware through the global `pdk` table — `pdk.cv_screen_width`,
  `pdk.cv_frame_configure`, `pdk.cv_frame_put`, `pdk.cast`. It reads the real
  screen size back from the console and prints it, then fills the frame with
  one triangle every `frame_render` — `FLAT_COLOURED` contrasts with
  `SAMPLE_TEXTURE` (vertex colour vs. texel), not with shading, so its three
  differently-coloured vertices Gouraud-interpolate into a red→green→blue
  gradient — the same configure-then-`cv_frame_put` shape `example-cpp.cpp`
  uses for its own
  sprites, built from a `rv_primitive` the script constructs itself with
  `pdk.new`. The C++ side flushes the frame right after the hook returns —
  `frame_render()` in `src/example-lua.cpp` is one `rv_cl_script_call()`
  followed by one `rv_cv_frame_flush()`, nothing else.

`pdk` is not a wrapper: it is a LuaJIT FFI table whose `__index` resolves
`pdk.cv_frame_put` to `ffi.C.rv_cv_frame_put` the first time it is touched —
the exact same exported symbol `example-cpp.cpp` calls as a plain C function.
Confirm it yourself:

```sh
$ nm -D --defined-only build/3dmppc | grep rv_cv_frame_put
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
./build/3dmppc build/example-lua.mppcdisc
```

Watch stderr for `pccl: lua machine up, 262144 byte(s) budgeted` on load, and
stdout for:

```
Hello from example lua!
example-lua: screen is 320x240 (read through pdk)
```

The second line is the proof: it only prints once `pdk.cv_screen_width` has
round-tripped into the console's real video controller and back.
