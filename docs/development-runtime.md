# The development runtime

`3dmppc` has one binary and one frame loop. `--dev` does not switch it into a
different build - it opens a second input path next to the platform's own
(the window's close button, a gamepad's Start): a line protocol on stdin,
answered on stdout. Without `--dev` the console reads no commands at all, so
a shipped binary is not controllable through this channel by construction.

```sh
./build/pconsole/3dmppc --dev build/example-lua.mppcdisc
```

- `--dev` opens the channel. Logs stay on stderr, exactly as without it -
  stdout is reserved for protocol lines from here on, and Lua's `print` is
  routed into the logger for exactly this reason: a script that printed to
  stdout would splice text into the answer stream.
- `--dev-paused` additionally stops the machine before frame 0. It requires
  `--dev` and is refused without it - there would be nothing listening for
  the command that could ever lift the pause.

## A worked session

Assume `example-lua/scripts/example-lua.lua` is mounted live
(an unpacked disc directory from `mppcburner build ... --unpacked`, passed as
the POSITIONAL argument - not `-d/--disc`, which mounts loose assets for the
built-in service test and loads no code) and the console was
started with `--dev`. Requests go to stdin, answers come back on stdout, one
line per line sent.

```
1 status
1 ok protocol=1 frame=0 mode=paused medium=live disc=6578616d706c652d6c7561 disc_hash=332b7bbf5565f50b pdk=0.2 entry_reloadable=1 entry_revision=0 entry_hash=da797e5ac9e965fb lua_used=83012 lua_budget=262144 chunks=1 error_seq=0 script_error=
```

The two hashes answer two different questions. `disc_hash` is the checksum of
the `disc.so` this process mapped - the same 16 characters the burner prints
when it stamps it - and `entry_hash` is a checksum of the bytes the Lua entry
chunk is running. Only the second one can change without a restart.

`disc` and `script_error` arrive as hex because either could contain a space
or a newline; every other field is a plain token.

Pause the run, then edit the file on disk and pick the change up from the
drive:

```
2 pause
2 ok

6 reload entry bytes 812
<812 bytes of the edited chunk follow the newline, no terminator>
6 ok entry_revision=1 entry_hash=dac227b078e5407b lua_used=101203

3 step
3 ok completed=1 frame=41 mode=paused

7 get frame_count
7 ok found=1 type=number value=41

4 resume
4 ok
```

Reload does not need step 1 (`2 pause`) to work - pause and reload are
independent commands - but stepping frame-by-frame to watch the new code
before letting it run free is the reason to pause first.

## The Pause key

The physical Pause key toggles the console's own pause in ANY mode, with no
`--dev` needed - it needs no development channel because it is not a
development feature. It is an operator's act on the machine, the same class
of input as closing the window: the disc never sees it, exactly as it never
sees a window close, and it drives the same pause state the `pause`/`resume`
requests drive when `--dev` is open. The two ways of pausing compose: the
key can pause a `--dev` run and the channel can resume it, or the other way
around - there is only one pause flag, not one per input source.

One consequence worth knowing before it surprises a script: a run started
with `--frames N` never reaches its frame budget while paused, because a
paused run advances no frames at all - a Pause key hit during an
otherwise-unattended `--frames` run hangs it exactly as a `--dev` client
that never sends `resume` would.

## Protocol, version 1

One request per line, LF-terminated: `<id> <verb> [args...]`. `<id>` is a
decimal request id the client picks; the console echoes it back unchanged so
answers can be matched to requests even if a client pipelines several before
reading a reply.

A request may carry bytes by ending its header with `bytes <n>`: exactly `n`
bytes follow the newline, with no terminator of their own, and the next
header begins immediately after them. Only `reload entry` uses this.

Answers are one line each, in request order:

```
<id> ok key=value ...
<id> err error=<token> ...
```

Any value that could contain a space, a newline or a NUL travels as
lowercase hex (`msg=<hex>`, `error_text=<hex>`), never as a raw token - the
protocol needs no escaping rules because of it.

### Verbs

| Request | Meaning |
| --- | --- |
| `<id> status` | session state: revision, hash, heap use, error count |
| `<id> pause` | stop at the next frame boundary |
| `<id> step` | run exactly one frame, then stay paused |
| `<id> resume` | continue running |
| `<id> reload entry` | re-read the entry script from the drive (medium must be a directory) |
| `<id> reload entry bytes <n>` | the next `n` bytes are the candidate script |
| `<id> asset <name>` | re-read one asset off the drive and tell the entry chunk it changed (medium must be a directory) |
| `<id> get frame_count` | read one top-level field of the persistent state table (`frame_count` is an example key, not a fixed field name - any key in the table can be asked for) |
| `<id> gc` | full collection, then report the script heap |
| `<id> quit` | shut the console down by its ordinary path |

`entry` is a literal selector, not a name to be resolved: v1 replaces the
entry chunk and nothing else. There is no verb for loading a second, unrelated
chunk over the development channel.

## Guarantees

- **Frame-boundary replacement.** A reload takes effect between frames, never
  mid-frame. Pause is not a precondition: pause and reload are independent
  commands, and issuing a reload never changes whether the run is paused
  afterward.
- **Code is atomic; effects are not.** A candidate must pass every check
  before the running code is let go: it compiles, its body runs, it returns a
  table, that table has `attach`, and `attach(state)` returns `true`. A
  failure at any of those leaves the old code running, untouched. But once a
  candidate's body or its `attach` has actually run, it may already have
  written into the state table or called into hardware - a voice may already
  be feeding the mixer, a video allocation the old code never made may already
  exist - and none of that can be undone. Every error answer therefore
  carries `effects=0` or `effects=1`. `effects=1` means "this may have
  happened", a conservative flag, not a proof that it did.
- **`attach` is the compatibility gate.** `attach` returning `false, "reason"`
  is the incompatible-state answer: the reload is refused and the old code
  keeps running. This is how a changed state-table layout gets caught - the
  console carries no schema for the state table and cannot detect a mismatch
  on its own; the script has to say so.
- **`attach` is required for reload, not for boot.** A script without
  `attach` runs fine as an entry chunk; the console just reports
  `entry_reloadable=0` for it, because there is nothing a later reload could
  call to check compatibility.
- **`asset <name>` reloads DATA, not code, and the console does half the
  job.** An asset is bytes the running game already put somewhere - a
  texture lives at a video address the game's own code chose, and the
  console has no way to know that address. So the console only re-reads the
  named asset off the drive and calls `asset_changed(name)` on the entry
  chunk at a frame boundary; the SCRIPT is the one that re-reads it through
  the drive (again, on its own) and re-uploads it to the address it
  remembers. `true` means the script handled it; `false, reason` is a
  refusal, reported as `error=asset_refused`, and leaves the old data in
  place. Like a code reload, effects inside the hook cannot be rolled back
  once it has started writing - `effects=` on the error answer follows the
  same conservative rule reload's does.
- **`asset` needs a live directory medium.** Only an unpacked disc directory
  (`mppcburner build ... --unpacked`) can have a file changed under a
  running console - in an archive the bytes cannot have moved, and the
  request is refused with `unsupported_medium`, the same token `reload
  entry` (drive form) uses for the same reason. A changed `.png` needs
  `mppcbaker` run again (by re-running the burner) before asking for the
  reload - a `.mppctex` is baker OUTPUT, and a symlink to the source `.png`
  is not one.
- **The hook is optional, like `attach`.** A chunk without `asset_changed`
  gets `error=no_asset_hook` when asked for an asset reload, exactly as a
  chunk without `attach` cannot be code-reloaded.
- **C++ cannot be hot swapped.** `status` reports `disc_hash`, the checksum of
  the loaded `disc.so`. The client is the one that knows what a fresh build's
  checksum should be; when they differ, the client is expected to restart
  rather than ask the console to do anything about it. A changed manifest or
  a changed budget also needs a restart: both are consumed once, at
  construction, and nothing in the protocol re-reads them.
- **Not covered.** No rollback of effects once they have happened. No hot
  reload of native (C++) code. No way to interrupt a hung `frame_update`. No
  way to interrupt a hung C or FFI call reached from a script. No way to
  restore a session's state across a process restart.

## Errors

Every `err` answer carries a stable `error=` token plus `effects=` and, where
useful, a `msg=<hex>` sentence. Each token also gets its own line in the log
at the moment it fires, so the same failure is legible from either end.

| Token | What it means |
| --- | --- |
| `protocol` | the request line could not be parsed (bad or missing id, unknown verb shape) |
| `payload_size` | the announced `bytes <n>` size was not a number, or exceeded the payload ceiling |
| `payload_timeout` | the announced payload did not fully arrive in time |
| `no_machine` | there is no script machine on this console (no `[budget.pccl]` on this disc) |
| `no_entry` | no entry chunk has ever been raised |
| `not_reloadable` | the current entry has no `attach` and cannot accept a reload |
| `in_call` | a reload was requested while a hook is still on the call stack |
| `unsupported_medium` | `reload entry` (drive form) or `asset` was asked of a non-directory medium |
| `no_asset` | `asset` named a file that does not exist on the drive |
| `no_asset_hook` | the entry chunk has no `asset_changed` at all |
| `asset_refused` | `asset_changed` returned `false` (with a reason) |
| `asset` | `asset_changed` raised instead of returning |
| `compile` | the candidate did not compile |
| `body` | the candidate compiled but its top-level body raised |
| `not_a_table` | the candidate's body ran but did not return a table |
| `no_attach` | the candidate table has no `attach` function |
| `attach` | `attach` raised instead of returning |
| `attach_refused` | `attach` returned `false` (with a reason) |
| `attach_contract` | `attach` returned something other than `true` or `false, reason` |
| `asset_contract` | `asset_changed` returned something other than `true` or `false, reason` |
| `nomem` | the script heap's budget was exceeded |
| `insn_ceiling` | the instruction ceiling installed for a reload attempt was hit (a hang guard, not a general watchdog) |

A C API stack imbalance is deliberately NOT one of these tokens: that is a
bug in the console itself, not a fault in the script, and reporting it as a
script error would send the developer looking in the wrong place.

## What needs a restart, and who notices

| Changed | Needs a process restart? | Who notices |
| --- | --- | --- |
| Entry Lua chunk (`.lua`/`.luac`) | No - `reload entry` | The client, by asking; `entry_revision` counts successful ones |
| An existing asset's bytes CHANGED | No - `asset <name>` | The client, by asking; the entry chunk's `asset_changed` decides whether it took |
| An asset ADDED or REMOVED | Yes | Nobody automatically - the medium's entry set is fixed at boot, and nothing re-reads it |
| `disc.so` (any C++ change) | Yes | The client, by comparing `disc_hash` against the checksum of its own fresh build |
| `disc.toml` manifest | Yes | Nobody automatically - it is read once, at construction |
| `[budget.*]` values | Yes | Nobody automatically - budgets are consumed once, at construction |

## Fixtures

[`tests/dev_runtime/fixtures/`](../tests/dev_runtime/fixtures/) holds one
minimal entry chunk per acceptance case, mapped to the `error=` token (or
lack of one) it is meant to prove; see
[its README](../tests/dev_runtime/fixtures/README.md) for the table. That
mapping is the source of truth this document agrees with - if the two ever
disagree, the fixtures README wins, because it is the one an acceptance run
actually executes against.
