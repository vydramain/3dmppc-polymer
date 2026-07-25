# third_party — code we did not write

Vendored dependencies: files copied into this repository verbatim from
elsewhere, so that a build needs no network, no package manager and no live
upstream. One subdirectory per dependency.

Everything else in this repository is written from scratch on purpose — no
engine, no framework. What lands here is the exception, and each exception
should be justifiable in a sentence.

## What is here

| Directory | What | Used by | License |
| --- | --- | --- | --- |
| [`stb/`](stb/) | `stb_image.h` — PNG/JPG/… decoder | `pdk/tools/mppcbaker` | public domain / MIT |

## Rules for adding something

1. **Vendor, do not fetch.** A download at configure time means the build breaks
   on a plane, behind a firewall, or the day the upstream repository is renamed.
   A file in the tree works forever.
2. **Record the origin.** Every subdirectory carries an `ORIGIN.md` naming the
   upstream URL, the exact commit or version, the date it was taken, and the
   SHA-256 of each file. Provenance that lives only in a commit message is
   provenance that gets lost.
3. **Keep the licence file** next to the code, unmodified.
4. **Do not edit vendored code.** If a change is unavoidable, put it in a patch
   file beside the original and say why in `ORIGIN.md` — otherwise the next
   update silently reverts it.
5. **Nothing here reaches the console.** `src/` links none of this: the console
   decodes no image format, parses no archive it did not define, and depends on
   nothing but SDL3. These are dependencies of the *tools*, and the separation
   is what keeps the machine small and auditable.

## Why not a git submodule

A submodule would pin the same commit, but it does not remove the dependency on
the network or on the upstream host — it only moves the failure from configure
time to clone time, and adds a `git submodule update --init` that everyone
forgets. Submodules earn their cost for large, actively released dependencies
one might contribute back to. A single stable public-domain header is not that.
