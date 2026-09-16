# Development-runtime fixtures

One minimal entry chunk per acceptance case, each a valid disc entry chunk
in the shape of
[`mppcdiscs/example-lua/scripts/example-lua.lua`](../../../mppcdiscs/example-lua/scripts/example-lua.lua):
a module table, `attach(state)`, and hooks where needed. No fixture here is
a complete disc by itself - each is only the entry script a `reload entry`
(or a boot) is pointed at; see
[`docs/development-runtime.md`](../../../docs/development-runtime.md) for
the protocol these prove.

| File | What it proves | Expected `error=` |
| --- | --- | --- |
| `ok_a.lua` | a well-formed entry, clear colour A; reloadable | (none - `reload` answers `ok`) |
| `ok_b.lua` | a well-formed entry, clear colour B; reloading A -> B preserves `state.frame_count` | (none - `reload` answers `ok`) |
| `syntax.lua` | does not parse | `compile` |
| `body_throws.lua` | parses, the top-level body raises | `body` |
| `not_a_table.lua` | the body runs but returns a number, not a table | `not_a_table` |
| `no_attach.lua` | a valid table with no `attach` at all | `no_attach` (and `reloadable=false` if raised as the boot entry) |
| `attach_false.lua` | `attach` writes nothing and returns `false, reason` | `attach_refused`, `effects=0` |
| `attach_throws_dirty.lua` | `attach` writes a state field, THEN raises | `attach`, `effects=1` |
| `attach_hangs.lua` | `attach` loops forever | `insn_ceiling` |
| `body_hangs.lua` | the top-level body loops forever | `insn_ceiling` |

This table and `docs/development-runtime.md` are meant to agree; if they
ever diverge, this one is the source of truth, because it is the one an
acceptance run executes against.
