// Resolves the run's rv_pcslots: built-in preset table -> --mode -> per-slot
// --mode_<slot> overrides, spelled with the name tables in rv_pcslots.hpp.
#pragma once

#include <string>

#include "rv_pboot_args.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

// Resolves `args` into `out`. Returns true on success. Returns false when the
// caller must return `exit_code` immediately (2 for a bad --mode or
// --mode_<slot> value, with a diagnostic and the usage text already printed).
bool rv_pboot_modes_resolve(const rv_pboot_args &args, rv_pcslots &out, int &exit_code);

// Renders the built-in preset table for --help as "name (platform impl)"
// pairs joined by "and", one entry per row of RV_PBOOT_BUILTIN_PRESETS.
std::string rv_pboot_builtin_presets_summary();

} // namespace rv_3dmppc
