// Resolves the run's rv_pcslots: built-in preset table -> --mode -> per-slot
// --mode_<slot> overrides, spelled with the name tables in rv_pcslots.hpp.
#pragma once

#include <string>

#include "rv_pboot_args.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

// Resolves `args` into `out`. Returns RV_OK on success, or rv_err code (RV_ERR_NOENT,
// RV_ERR_INVAL, RV_ERR_IO) when resolution fails; exit_code is set to 2 and diagnostic
// and usage are printed when an rv_err is returned.
int rv_pboot_modes_resolve(const rv_pboot_args &args, rv_pcslots &out, int &exit_code);

// Renders the built-in preset table for --help as "name (platform impl)"
// pairs joined by "and", one entry per row of RV_PBOOT_BUILTIN_PRESETS.
std::string rv_pboot_builtin_presets_summary();

} // namespace rv_3dmppc
