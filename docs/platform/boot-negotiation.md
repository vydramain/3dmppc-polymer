# Disc boot — resource negotiation

**Status: draft. Not in the code yet.** The document records the decisions made
about how the console decides whether the machine it runs on can boot the
inserted disc, and what it will limit itself to while doing so. The implemented
boot path is described in [`disc-loading.md`](disc-loading.md); this is what
should sit between mounting the archive and bringing up the code.

---

## The problem

A disc declares how much resources it needs. Before booting, the console looks
at the machine it is running on and answers: can it provide this or not. If it
can't — the disc doesn't start. The requirement is the same both for the
current software mode and for the future hardware mode.

---

## Three quantities instead of one config

Today `rv_pconsole_conf` plays all three roles at once, and therefore none of
them properly.

| Quantity       | What it is                             | Where it comes from                |
| -------------- | --------------------------------------- | ----------------------------------- |
| **request**    | what the disc wants                     | manifest `[budget.*]`; no disc — default values |
| **capability** | what the machine actually gives         | host measurement at startup         |
| **grant**      | what the console limits itself to for the session | result of negotiation      |

Controllers are built **from grant**, and only from it. `conf` stops being an
input to the console and becomes a result.

## Units: the disc asks the console, the console bargains with the machine

`request` is entirely in **console** units — console VRAM, voices, screen,
`frame_capacity`. There are no host numbers in the manifest.

This is what makes a disc uniform across any machine. The number "1 MB of
console VRAM" means the same thing everywhere, and each backend knows exactly
what that megabyte costs on it. A host number has no such property: 32 GB of
host RAM on a hardware backend describes neither what the disc needs nor what
the console will spend — checking it makes no sense.

The other side of the coin: "works the same" rests not on resources but on
**computation rules**. The console specification must fix rasterization,
rounding, ordering at equal depth, and sound mixing, or two backends with a
fully satisfied budget will produce different pictures.

## A backend supplies routes, not a formula

A backend assigns each console memory class to a host pool and names a
coefficient. After that the operation is always the same: sum everything that
lands in one pool and compare it against that pool's capacity.

- **software** — all classes are routed to a single pool (host RAM), so they
  are summed;
- **hardware** — classes are routed to their own pools, so each is compared
  against itself.

There is no `if (software)` branch in the negotiation: there is a routing
table supplied by the backend. The table is chosen **after measurement**, not
hardcoded into the backend — on an integrated GPU, VRAM physically *is* host
RAM, and the hardware backend routes it into the same pool as RAM, where it is
summed again.

## Where the boundary between disc memory and console memory runs

By address ownership, not by the manifest.

- **Disc memory** — what the disc gets an address for and manages itself: the
  VRAM pool (`video_asset_malloc`) and the sound pool. Isolation is real: the
  disc receives an offset, not a pointer, and has no host allocator through
  the contract.
- **Console memory** — everything else, including structures whose size is
  dictated by the disc but whose addresses it never sees: framebuffer,
  ordering table, frame primitive list, pool block metadata, the unpacked
  `.so`, SDL, the Lua VM.

Hence: the console's own consumption is **not a constant**. Only the base is
constant (the process, SDL, the VM, fixed-size buffers); the rest is a
function of grant. What is compared against capability is

```
console_base + overhead(grant) + disc_pools
```

otherwise the console on a machine with 32 GB would honestly promise the disc
32 GB and die itself.

The whole sum goes out into the total process footprint; the disc only sees
its own pools.

## A reservation, not an estimate

Since the console answers "the machine will provide this" at boot time, the
number has to be a real reservation: memory is taken from the host at startup,
in full, before the first frame. Otherwise a boot-time refusal guarantees
nothing — it said "yes", and at the fortieth minute the host handed the memory
to someone else.

Consequence: "approximately" doesn't survive in the manifest. The number is a
ceiling, exact from above. The disc is entitled to take less, but not one byte
more; exceeding it is `RV_ERR_NOMEM` from the console, and the host is not
touched in that case.

Any setting that can be adjusted at runtime (resolution, for example) enters
the budget as the **worst case**. The console reserves the framebuffer at its
maximum once, and a resolution change in-game is a move within the space
already reserved. `RV_ERR_NOMEM` in the middle of a session becomes impossible
by construction.

## No auto-downgrade

Graphics settings must not be a function of how much the machine managed to
give. The temptation of "the machine is weak, quietly lower the resolution and
launch" breaks exactly what the whole thing was built for: the disc starts
looking different on different machines because of hardware, not because of
the player's choice. The order is only this: the disc declares a ceiling →
the machine provides it in full, or gets a refusal.

The player can adjust settings, but within what the disc declared, and their
choice affects neither the grant nor the reservation — the memory is already
taken at the maximum.

---

## Diagram

```
                          STARTUP
                            |
                            v
             MEASURE MACHINE -> capability
             host pools in host units: RAM, GPU-mem, ...
                            |
                            v
                       is there a disc?
                     /             \
                   no               yes
                    |                |
                    v                v
            default request   read ONLY the manifest
                    |         (do not bring up code)
                    \                /
                     v              v
                 REQUEST in CONSOLE units
                 VRAM, AUDIORAM, screen(max), frame_capacity, ...
                            |
                            v
              BACKEND: class -> host pool routes
              software: all classes into one pool
              hardware: each into its own
                            |
                            v
              COST = console base
                   + overhead(request)   framebuffer, OT, list
                   + disc pools
                            |
                            v
                 COMPARISON PER POOL
                     /              \
              not enough          enough
                    |                 |
                    v                 v
         REFUSAL: "pool X needs     GRANT = request
         N, machine gives M"              |
         disc does not start              v
                                 RESERVATION: allocate everything
                                 at once, before the first frame
                                        |
                                        v
                                 BUILD CONTROLLERS
                                 sizes only from grant
                                        |
                                        v
                                 BRING UP CODE (dlopen)
                                        |
                                        v
                                      RUN
                                        |
                        disc asks beyond its pool
                                        |
                                        v
                                  RV_ERR_NOMEM
                            (the host is not touched)
```

A refusal always names a concrete pool: on software it is "host RAM is not
enough for the sum of three classes", on hardware — "video memory specifically
is not enough".

---

## What is not yet decided

Between "measurement" and "building controllers" the console already exists,
but has no hardware yet. The current form does not survive this: the
`rv_pconsole` constructor builds all five controllers at once, and `rv_pcpool`
allocates its whole buffer in its own constructor — that is, memory is handed
out before the manifest is read. It needs to be decided who constructs whom
and in which states the console lives.
