# hello — the smallest complete disc

A minimal `.mppcdisc` that exists to prove the whole path works, and to be the
thing you copy when starting a real one.

```sh
# build the developer tools once
cmake -S pdk/tools -B pdk/tools/build && cmake --build pdk/tools/build

# burn this directory into a disc
./pdk/tools/build/mppcburner/mppcburner build mppcdiscs/hello -o build/hello.mppcdisc

# look inside it without unpacking
./pdk/tools/build/mppcburner/mppcburner inspect build/hello.mppcdisc

# run it — the console does not know this game's name
./build/3dmppc build/hello.mppcdisc
```

Press **Esc** (or **Option/Start** on a pad) to quit.

## What is here

```
hello/
  disc.toml        the manifest: what to compile, what to bake, what to copy
  src/hello.cpp    the whole game — one file
  assets/
    badge.png      baked into badge.mppctex on the way into the archive
    greeting.txt   copied in verbatim
```

## What it proves, and how you can see it

| On screen | Means |
| --- | --- |
| three squares in the middle | `rv_cv` — the same texture drawn CLAMP / TILE / STRETCH; the missing quadrant is the cut-out transparency rule |
| the cyan bar at the bottom | `rv_cd` — `greeting.txt` was read back off the medium; its length is the bar's width |
| the window closing on Esc | `rv_cio` — and the disc asking to stop, rather than the console deciding |
| it ran at all | the ABI handshake, `dlopen`, and the factory in `RV_DISC_EXPORT` |

If the squares are missing, the texture did not survive the pipeline. If the bar
is missing, the archive mounted but the bytes did not come back.

## Two things worth copying from `src/hello.cpp`

**The last line.** `RV_DISC_EXPORT(hello::rv_dmain)` is what makes this a
disc rather than a library: it plants the two `extern "C"` symbols the console
looks up after `dlopen`, and they are the only symbols reachable from outside.

**`disc_shutdown()`.** Everything `disc_initialize` took is given back there, not
in a destructor. After that hook returns, the console may unload the disc's code
entirely — and a destructor belonging to unmapped code cannot run.

## The boundary

`hello` includes `pdk/` (the contract) and the standard library. Nothing else.
There is no console header here, and the burner does not put the console's
include path on the project it generates, so reaching for one does not compile —
the rule that a game plays *on* the console rather than links *into* it is
enforced by the build, not by discipline.

A disc may also use [`pdklib/`](../../pdklib/) — matrices, camera, `.obj` parsing,
text — which is written against the same contract. `hello` does not, only
because it does not need to.
