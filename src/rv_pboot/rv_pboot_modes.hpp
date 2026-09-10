// Resolves the run's rv_pcslots: built-in preset table -> --mode -> per-slot
// --mode_<slot> overrides. String<->enum tables for every slot impl live
// here, one row per value, and are the only place that vocabulary is spelled.
#pragma once

#include "rv_pboot_args.hpp"
#include "rv_pconsole/rv_pconsole_conf.hpp"

namespace rv_3dmppc
{

// Resolves `args` into `out`. Returns true on success. Returns false when the
// caller must return `exit_code` immediately (2 for a bad --mode or
// --mode_<slot> value, with a diagnostic and the usage text already printed).
bool rv_pboot_modes_resolve(const rv_pboot_args &args, rv_pcslots &out, int &exit_code);

// Name of an implementation, for logging (rv_pboot_mode.cpp's report line).
const char *rv_pboot_impl_name(rv_pcca_impl impl);
const char *rv_pboot_impl_name(rv_pccv_impl impl);
const char *rv_pboot_impl_name(rv_pccio_impl impl);
const char *rv_pboot_impl_name(rv_pccl_impl impl);

} // namespace rv_3dmppc
